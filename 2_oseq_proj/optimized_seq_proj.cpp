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

// --- Optimized Sequential Radix Sort (Histogram-based) ---
void radix_sort_pass(std::vector<Record>& data, int byte_num) {
    int n = data.size();
    if (n == 0) return;

    std::vector<Record> temp_buffer(n);
    const int BUCKET_SIZE = 256;

    // Step 1: Create histogram
    std::vector<int> counts(BUCKET_SIZE, 0);
    for (int i = 0; i < n; ++i) {
        counts[data[i].key_byte(byte_num)]++;
    }

    // Step 2: Compute prefix sum (offsets)
    std::vector<int> offsets(BUCKET_SIZE, 0);
    for (int i = 1; i < BUCKET_SIZE; ++i) {
        offsets[i] = offsets[i - 1] + counts[i - 1];
    }

    // Step 3: Place elements in sorted order into temp buffer
    // Note: This step is hard to vectorize due to random access writes (scatter)
    for (int i = 0; i < n; ++i) {
        int bucket_index = data[i].key_byte(byte_num);
        temp_buffer[offsets[bucket_index]++] = data[i];
    }

    // Step 4: Copy sorted data back, with SIMD optimization hint
    #pragma omp simd
    for (int i = 0; i < n; ++i) {
        data[i] = temp_buffer[i];
    }
}

void radix_sort(std::vector<Record>& data) {
    // 10 passes for 10-byte keys, starting from the least significant byte (index 9) down to 0
    for (int i = 9; i >= 0; --i) {
        radix_sort_pass(data, i);
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