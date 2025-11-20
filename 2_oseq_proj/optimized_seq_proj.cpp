#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include "json.hpp"
#include <omp.h> // For omp simd

using json = nlohmann::json;

// --- Utility Functions (unchanged) ---
std::vector<unsigned int> read_data(const std::string& filename) {
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
    std::vector<unsigned int> data;
    data.reserve(num_records);
    char buffer[RECORD_SIZE];
    for (long long i = 0; i < num_records; ++i) {
        if (!file.read(buffer, RECORD_SIZE)) {
            std::cerr << "Error reading record " << i << " from file." << std::endl;
            exit(1);
        }
        data.push_back(*reinterpret_cast<unsigned int*>(buffer));
    }
    return data;
}

bool is_sorted(const std::vector<unsigned int>& data) {
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] > data[i + 1]) {
            return false;
        }
    }
    return true;
}

// --- Optimized Sequential Merge Sort (Iterative with Ping-Pong Buffer) ---
void merge_sort(std::vector<unsigned int>& data) {
    int n = data.size();
    if (n <= 1) return;

    std::vector<unsigned int> temp_buffer(n);
    std::vector<unsigned int>* src = &data;
    std::vector<unsigned int>* dst = &temp_buffer;

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
        std::copy(src->begin(), src->end(), data.begin());
    }
}

// --- Optimized Sequential Radix Sort (Histogram-based) ---
void radix_sort_pass(std::vector<unsigned int>& data, int byte_num) {
    int n = data.size();
    if (n == 0) return;

    std::vector<unsigned int> temp_buffer(n);
    int shift = byte_num * 8;
    const int BUCKET_SIZE = 256;

    // Step 1: Create histogram
    std::vector<int> counts(BUCKET_SIZE, 0);
    for (int i = 0; i < n; ++i) {
        counts[(data[i] >> shift) & 0xFF]++;
    }

    // Step 2: Compute prefix sum (offsets)
    std::vector<int> offsets(BUCKET_SIZE, 0);
    for (int i = 1; i < BUCKET_SIZE; ++i) {
        offsets[i] = offsets[i - 1] + counts[i - 1];
    }

    // Step 3: Place elements in sorted order into temp buffer
    for (int i = 0; i < n; ++i) {
        int bucket_index = (data[i] >> shift) & 0xFF;
        temp_buffer[offsets[bucket_index]++] = data[i];
    }

    // Step 4: Copy sorted data back, with SIMD optimization hint
    #pragma omp simd
    for (int i = 0; i < n; ++i) {
        data[i] = temp_buffer[i];
    }
}

void radix_sort(std::vector<unsigned int>& data) {
    for (int i = 0; i < 4; ++i) { // 4 passes for 32-bit integers
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