#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <omp.h>
#include "json.hpp"

using json = nlohmann::json;

// --- Utility Functions ---
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

// --- CORRECTED: Simplified Hybrid Sort/Merge ---
// A parallel sort that divides data into chunks, sorts them locally, then merges them in parallel.
void hybrid_sort_merge(std::vector<unsigned int>& data) {
    int n = data.size();
    if (n == 0) return;

    std::vector<unsigned int> temp_buffer(n);
    bool in_data = true;

    #pragma omp parallel
    {
        int num_threads = omp_get_num_threads();
        int thread_id = omp_get_thread_num();
        
        int block_size = (n + num_threads - 1) / num_threads;
        int start = thread_id * block_size;
        int end = std::min(start + block_size, n);

        // Phase 1: Each thread sorts its local chunk
        if (start < end) {
            std::sort(data.begin() + start, data.begin() + end);
        }
        #pragma omp barrier
    }

    // Phase 2: Iterative parallel merge
    for (int merge_size = 1; merge_size < n; merge_size *= 2) {
        std::vector<unsigned int>& src = in_data ? data : temp_buffer;
        std::vector<unsigned int>& dst = in_data ? temp_buffer : data;
        
        #pragma omp parallel for
        for (int i = 0; i < n; i += 2 * merge_size) {
            int start1 = i;
            int end1 = std::min(start1 + merge_size, n);
            int start2 = end1;
            int end2 = std::min(start2 + merge_size, n);
            
            // std::merge handles empty ranges correctly
            std::merge(src.begin() + start1, src.begin() + end1,
                       src.begin() + start2, src.begin() + end2,
                       dst.begin() + start1);
        }
        in_data = !in_data;
    }

    // Copy final result back to data if needed
    if (!in_data) {
        #pragma omp parallel for
        for (int i = 0; i < n; ++i) {
            data[i] = temp_buffer[i];
        }
    }
}

// --- CORRECTED: Efficient Parallel Radix Sort ---
// Uses a 2D histogram approach with prefix sums for O(t^2) offset calculation
void scalable_radix_sort_pass(std::vector<unsigned int>& data, int byte_num) {
    int n = data.size();
    if (n == 0) return;

    std::vector<unsigned int> temp_buffer(n);
    int shift = byte_num * 8;
    const int BUCKET_SIZE = 256;

    int num_threads = omp_get_max_threads();
    
    // 2D histogram: thread_histograms[thread_id][bucket]
    std::vector<std::vector<int>> thread_histograms(num_threads, std::vector<int>(BUCKET_SIZE, 0));
    
    // Step 1: Each thread builds its private histogram
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int chunk_size = (n + num_threads - 1) / num_threads;
        int start_idx = thread_id * chunk_size;
        int end_idx = std::min(start_idx + chunk_size, n);

        for (int i = start_idx; i < end_idx; ++i) {
            int bucket = (data[i] >> shift) & 0xFF;
            thread_histograms[thread_id][bucket]++;
        }
    }

    // Step 2: Compute prefix sums to determine placement offsets
    // offset[thread_id][bucket] = where thread_id should start placing elements in bucket
    std::vector<std::vector<int>> offsets(num_threads, std::vector<int>(BUCKET_SIZE, 0));
    
    for (int bucket = 0; bucket < BUCKET_SIZE; ++bucket) {
        int current_offset = 0;
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            offsets[thread_id][bucket] = current_offset;
            current_offset += thread_histograms[thread_id][bucket];
        }
    }

    // Step 3: Each thread places its elements using precomputed offsets
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int chunk_size = (n + num_threads - 1) / num_threads;
        int start_idx = thread_id * chunk_size;
        int end_idx = std::min(start_idx + chunk_size, n);

        // Create thread-local counters for placement within each bucket
        std::vector<int> local_offsets = offsets[thread_id];

        for (int i = start_idx; i < end_idx; ++i) {
            int bucket = (data[i] >> shift) & 0xFF;
            temp_buffer[local_offsets[bucket]++] = data[i];
        }
    }

    // Step 4: Copy sorted data back to original array
    #pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        data[i] = temp_buffer[i];
    }
}

void scalable_radix_sort(std::vector<unsigned int>& data) {
    for (int i = 0; i < 4; ++i) {
        scalable_radix_sort_pass(data, i);
    }
}

// --- Main Driver ---
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <algorithm> <threads> <filename>" << std::endl;
        return 1;
    }
    std::string algorithm = argv[1];
    int threads = std::stoi(argv[2]);
    std::string filename = argv[3];
    
    omp_set_num_threads(threads);

    auto data = read_data(filename);
    size_t N = data.size();

    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "hybrid_merge_sort") {
        hybrid_sort_merge(data);
    } else if (algorithm == "scalable_radix_sort") {
        scalable_radix_sort(data);
    } else {
        std::cerr << "Unknown algorithm: " << algorithm << std::endl;
        return 1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_ms = end - start;

    bool sorted_correctly = is_sorted(data);
    
    double duration_s = duration_ms.count() / 1000.0;
    // Throughput: (N in millions) / (time in seconds) = M-keys/second
    double mkeys_per_second = (duration_s > 0) ? (N / 1e6 / duration_s) : 0;

    json result = {
        {"algorithm", algorithm},
        {"N", N},
        {"threads", threads},
        {"time_ms", duration_ms.count()},
        {"mkeys_per_s", mkeys_per_second},
        {"correct", sorted_correctly}
    };
    std::cout << result.dump() << std::endl;
    
    return 0;
}