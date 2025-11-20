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
    std::vector<unsigned int> data;
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

// --- Basic OpenMP Merge Sort ---
void sort_merge(std::vector<unsigned int>& data) {
    int num_threads = omp_get_max_threads();
    int n_global = data.size();
    
    std::vector<int> sendcounts(num_threads);
    std::vector<int> displs(num_threads, 0);
    int chunk_size = n_global / num_threads;
    int remainder = n_global % num_threads;

    for (int i = 0; i < num_threads; ++i) {
        sendcounts[i] = chunk_size + (i < remainder ? 1 : 0);
        if (i > 0) {
            displs[i] = displs[i - 1] + sendcounts[i - 1];
        }
    }

    // Each thread sorts its local chunk (implicit barrier at end)
    #pragma omp parallel for schedule(static, 1)
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        std::sort(data.begin() + displs[thread_id],
                  data.begin() + displs[thread_id] + sendcounts[thread_id]);
    }

    // Tree-based merging: perform pairwise merges in parallel, but
    // update shared metadata (sendcounts/displs) only after the
    // parallel phase to avoid race conditions that corrupt memory.
    for (int step = 1; step < num_threads; step *= 2) {
        // Prepare a vector to capture merged sizes for this step.
        std::vector<int> merged_sizes = sendcounts; // start with current sizes

        #pragma omp parallel for schedule(static, 1)
        for (int thread_id = 0; thread_id < num_threads; thread_id += 2 * step) {
            if (thread_id + step < num_threads) {
                int left_start = displs[thread_id];
                int left_end = displs[thread_id] + sendcounts[thread_id];
                int right_start = displs[thread_id + step];
                int right_end = displs[thread_id + step] + sendcounts[thread_id + step];

                int left_len = left_end - left_start;
                int right_len = right_end - right_start;
                std::vector<unsigned int> merged(left_len + right_len);
                std::merge(data.begin() + left_start, data.begin() + left_end,
                           data.begin() + right_start, data.begin() + right_end,
                           merged.begin());

                // Write merged results back into the main array
                std::copy(merged.begin(), merged.end(), data.begin() + left_start);

                // Record new size for this merged region (thread-local index)
                merged_sizes[thread_id] = left_len + right_len;
                // Mark the right-hand chunk as consumed
                merged_sizes[thread_id + step] = 0;
            }
        }

        // Single-threaded update of sendcounts and displs to avoid races
        for (int i = 0; i < num_threads; ++i) {
            sendcounts[i] = merged_sizes[i];
            if (i == 0) displs[i] = 0;
            else displs[i] = displs[i - 1] + sendcounts[i - 1];
        }
    }
}

// --- Basic OpenMP Radix Sort ---
void radix_sort(std::vector<unsigned int>& data) {
    int num_threads = omp_get_max_threads();
    int n_global = data.size();

    std::vector<int> sendcounts(num_threads);
    std::vector<int> displs(num_threads, 0);
    int chunk_size = n_global / num_threads;
    int remainder = n_global % num_threads;
    for (int i = 0; i < num_threads; ++i) {
        sendcounts[i] = chunk_size + (i < remainder ? 1 : 0);
        if (i > 0) displs[i] = displs[i - 1] + sendcounts[i - 1];
    }

    const int BUCKET_SIZE = 256;
    std::vector<unsigned int> temp_data(n_global);

    for (int i = 0; i < 4; ++i) { // 4 passes for 32-bit integers
        int shift = i * 8;

        // 1. Thread-local histograms
        std::vector<std::vector<int>> thread_hists(num_threads, std::vector<int>(BUCKET_SIZE, 0));
        
        #pragma omp parallel for schedule(static, 1)
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            for (int j = displs[thread_id]; j < displs[thread_id] + sendcounts[thread_id]; ++j) {
                int bucket = (data[j] >> shift) & 0xFF;
                thread_hists[thread_id][bucket]++;
            }
        }

        // 2. Global histogram via reduction
        std::vector<int> global_hist(BUCKET_SIZE, 0);
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            for (int b = 0; b < BUCKET_SIZE; ++b) {
                global_hist[b] += thread_hists[thread_id][b];
            }
        }

        // 3. Compute offsets for each bucket
        std::vector<int> bucket_offsets(BUCKET_SIZE, 0);
        int offset = 0;
        for (int b = 0; b < BUCKET_SIZE; ++b) {
            bucket_offsets[b] = offset;
            offset += global_hist[b];
        }

        // 4. Place elements in temporary array based on bucket
        std::vector<std::vector<int>> bucket_positions(num_threads, std::vector<int>(BUCKET_SIZE, 0));
        
        #pragma omp parallel for schedule(static, 1)
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            std::vector<int> local_positions(BUCKET_SIZE, 0);
            for (int j = displs[thread_id]; j < displs[thread_id] + sendcounts[thread_id]; ++j) {
                int bucket = (data[j] >> shift) & 0xFF;
                local_positions[bucket]++;
            }
            bucket_positions[thread_id] = local_positions;
        }

        #pragma omp parallel for schedule(static, 1)
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            std::vector<int> positions = bucket_offsets;
            
            // Accumulate positions from previous threads
            for (int t = 0; t < thread_id; ++t) {
                for (int b = 0; b < BUCKET_SIZE; ++b) {
                    positions[b] += bucket_positions[t][b];
                }
            }
            
            for (int j = displs[thread_id]; j < displs[thread_id] + sendcounts[thread_id]; ++j) {
                int bucket = (data[j] >> shift) & 0xFF;
                temp_data[positions[bucket]++] = data[j];
            }
        }

        data = temp_data;
    }
}


// --- Main Driver ---
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <algorithm> <filename>" << std::endl;
        std::cerr << "Note: Number of threads is determined by OMP_NUM_THREADS." << std::endl;
        std::cerr << "Algorithms: merge_sort, radix_sort" << std::endl;
        return 1;
    }
    std::string algorithm = argv[1];
    std::string filename = argv[2];
    
    auto data = read_data(filename);
    size_t N = data.size();

    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "merge_sort") {
        sort_merge(data);
    } else if (algorithm == "radix_sort") {
        radix_sort(data);
    } else {
        std::cerr << "Unknown algorithm: " << algorithm << std::endl;
        std::cerr << "Algorithms: merge_sort, radix_sort" << std::endl;
        return 1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> duration_ms = end - start;
    bool sorted_correctly = is_sorted(data);
    
    double duration_s = duration_ms.count() / 1000.0;
    double mkeys_per_second = (duration_s > 0) ? (N / 1e6 / duration_s) : 0;

    int num_threads = omp_get_max_threads();

    json result = {
        {"Title", "Basic Parallel Sorter"},
        {"algorithm", algorithm},
        {"N", N},
        {"threads", num_threads},
        {"time_ms", duration_ms.count()},
        {"mkeys_per_s", mkeys_per_second},
        {"correct", sorted_correctly}
    };
    std::cout << result.dump() << std::endl;

    return 0;
}