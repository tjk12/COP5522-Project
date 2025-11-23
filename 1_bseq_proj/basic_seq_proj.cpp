#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstring> // For std::memcmp
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
        if (data[i+1] < data[i]) { // If next is smaller than current, it's unsorted
            return false;
        }
    }
    return true;
}

// --- Basic Sequential Merge Sort (Recursive) ---
void merge_sort_recursive(std::vector<Record>& data, std::vector<Record>& temp, int left, int right) {
    if (left >= right) {
        return;
    }
    int mid = left + (right - left) / 2;
    merge_sort_recursive(data, temp, left, mid);
    merge_sort_recursive(data, temp, mid + 1, right);
    std::merge(data.begin() + left, data.begin() + mid + 1,
               data.begin() + mid + 1, data.begin() + right + 1,
               temp.begin() + left);
    std::copy(temp.begin() + left, temp.begin() + right + 1, data.begin() + left);
}

void merge_sort(std::vector<Record>& data) {
    if (data.empty()) return;
    std::vector<Record> temp(data.size());
    merge_sort_recursive(data, temp, 0, data.size() - 1);
}

// --- Basic Sequential Radix Sort (Standard LSD with Counting Sort) ---
void radix_sort_pass(std::vector<Record>& data, std::vector<Record>& temp, int byte_index) {
    int n = data.size();
    const int BUCKET_SIZE = 256;
    
    // 1. Histogram
    std::vector<int> count(BUCKET_SIZE, 0);
    for (int i = 0; i < n; ++i) {
        count[data[i].key_byte(byte_index)]++;
    }

    // 2. Prefix Sum (Offsets)
    std::vector<int> offsets(BUCKET_SIZE);
    offsets[0] = 0;
    for (int i = 1; i < BUCKET_SIZE; ++i) {
        offsets[i] = offsets[i - 1] + count[i - 1];
    }

    // 3. Scatter to temp
    for (int i = 0; i < n; ++i) {
        int b = data[i].key_byte(byte_index);
        temp[offsets[b]++] = data[i];
    }

    // 4. Copy back
    data = temp;
}

void radix_sort(std::vector<Record>& data) {
    if (data.empty()) return;
    std::vector<Record> temp(data.size());
    
    // 10 passes for 10-byte keys
    for (int i = 9; i >= 0; --i) {
        radix_sort_pass(data, temp, i);
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
        {"Title", "Basic Sequential Sorter"},
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