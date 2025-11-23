#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <cstring> // For std::memcmp
#include <omp.h> // For omp simd
#include "json.hpp"

using json = nlohmann::json;

// --- Data Structures ---
struct Record {
    unsigned char data[100];

    // Compare first 10 bytes (Key)
    bool operator<(const Record& other) const {
        return std::memcmp(data, other.data, 10) < 0;
    }
    
    // Helper for Radix Sort to get a specific byte of the key
    unsigned char key_byte(int byte_index) const {
        return data[byte_index];
    }
};

// --- Utility Functions ---
std::vector<Record> read_data(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        exit(1);
    }
    const int RECORD_SIZE = 100;
    file.seekg(0, std::ios::end);
    long long file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    long long num_records = file_size / RECORD_SIZE;
    
    std::vector<Record> data(num_records);
    
    // Read in chunks to avoid issues with reading > 2GB/4GB in a single call on Windows
    char* buffer_ptr = reinterpret_cast<char*>(data.data());
    long long bytes_remaining = file_size;
    const long long CHUNK_SIZE = 1024 * 1024 * 1024; // 1 GB chunks

    while (bytes_remaining > 0) {
        long long bytes_to_read = std::min(bytes_remaining, CHUNK_SIZE);
        if (!file.read(buffer_ptr, bytes_to_read)) {
            std::cerr << "Error reading file." << std::endl;
            exit(1);
        }
        buffer_ptr += bytes_to_read;
        bytes_remaining -= bytes_to_read;
    }
    
    return data;
}

bool is_sorted(const std::vector<Record>& data) {
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i+1] < data[i]) {
            return false;
        }
    }
    return true;
}

// --- Optimized Sequential Merge Sort (Iterative with Ping-Pong Buffer) ---
void merge_sort(std::vector<Record>& data) {
    int n = data.size();
    if (n <= 1) return;

    std::vector<Record> temp_buffer(n);
    std::vector<Record>* src = &data;
    std::vector<Record>* dst = &temp_buffer;

    // Start with small sorts, then merge iteratively
    const int initial_sort_size = 16;
    for (int i = 0; i < n; i += initial_sort_size) {
        std::sort(src->begin() + i, src->begin() + std::min(i + initial_sort_size, n));
    }

    for (int merge_size = initial_sort_size; merge_size < n; merge_size *= 2) {
        for (int i = 0; i < n; i += 2 * merge_size) {
            int start1 = i;
            int end1 = std::min(start1 + merge_size, n);
            int start2 = end1;
            int end2 = std::min(start2 + merge_size, n);
            
            std::merge((*src).begin() + start1, (*src).begin() + end1,
                       (*src).begin() + start2, (*src).begin() + end2,
                       (*dst).begin() + start1);
        }
        std::swap(src, dst); // Ping-pong
    }

    // If the final result is in the temp buffer, copy it back
    if (src != &data) {
        #pragma omp simd
        for (int i = 0; i < n; ++i) {
            data[i] = (*src)[i];
        }
    }
}

// --- Optimized Sequential Radix Sort (Ping-Pong) ---
void radix_sort(std::vector<Record>& data) {
    int n = data.size();
    if (n == 0) return;

    std::vector<Record> buffer(n);
    bool in_data = true; // true if valid data is in 'data', false if in 'buffer'

    // 10 passes for 10-byte keys
    for (int byte_idx = 9; byte_idx >= 0; --byte_idx) {
        const std::vector<Record>& src = in_data ? data : buffer;
        std::vector<Record>& dst = in_data ? buffer : data;

        // 1. Histogram
        int counts[256] = {0};
        for (int i = 0; i < n; ++i) {
            counts[src[i].key_byte(byte_idx)]++;
        }

        // 2. Prefix Sum (Offsets)
        int offsets[256];
        offsets[0] = 0;
        for (int i = 1; i < 256; ++i) {
            offsets[i] = offsets[i - 1] + counts[i - 1];
        }

        // 3. Scatter
        for (int i = 0; i < n; ++i) {
            int b = src[i].key_byte(byte_idx);
            dst[offsets[b]++] = src[i];
        }

        in_data = !in_data;
    }

    // If final result is in buffer, copy back to data
    if (!in_data) {
        data = buffer;
    }
}

// --- Main Driver ---
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <algorithm> <filename>" << std::endl;
        std::cerr << "Algorithms: merge_sort, radix_sort" << std::endl;
        return 1;
    }
    std::string algorithm = argv[1];
    std::string filename = argv[2];
    
    auto data = read_data(filename);
    size_t N = data.size();

    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "merge_sort") {
        merge_sort(data);
    } else if (algorithm == "radix_sort") {
        radix_sort(data);
    } else {
        std::cerr << "Unknown algorithm: " << algorithm << std::endl;
        return 1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_ms = end - start;

    bool sorted_correctly = is_sorted(data);
    
    double duration_s = duration_ms.count() / 1000.0;
    double mkeys_per_second = (duration_s > 0) ? (N / 1e6 / duration_s) : 0;

    json result = {
        {"Title", "Optimized Sequential Sorter"},
        {"algorithm", algorithm},
        {"N", N},
        {"threads", 1}, // Always 1 for sequential implementation
        {"time_ms", duration_ms.count()},
        {"mkeys_per_s", mkeys_per_second},
        {"correct", sorted_correctly}
    };
    std::cout << result.dump() << std::endl;
    
    return 0;
}