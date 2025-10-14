#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <omp.h>
#include "json.hpp" // Assuming json.hpp is in an 'include' directory

using json = nlohmann::json;

// --- Utility Functions ---
std::vector<unsigned int> read_data(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        exit(1);
    }
    const int RECORD_SIZE = 100; // Each record is 100 bytes
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
        // Extract the 4-byte key (assuming it's at the beginning of the 100-byte record)
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

// --- Scalable Hybrid Sort/Merge ---
// A parallel sort that divides data into chunks, sorts them locally, then merges them in parallel.
void hybrid_sort_merge(std::vector<unsigned int>& data) {
    int n = data.size();
    if (n == 0) return;

    // Use a temporary buffer for merging, swapping source and destination
    std::vector<unsigned int> temp_buffer(n);
    bool in_data = true; // Flag to track if the current sorted state is in 'data' or 'temp_buffer'

    #pragma omp parallel
    {
        int num_threads = omp_get_num_threads();
        int thread_id = omp_get_thread_num();
        
        // Ensure that block_size is at least 1, even for small N
        int block_size = (n + num_threads - 1) / num_threads;
        int start = thread_id * block_size;
        int end = std::min(start + block_size, n);

        // Phase 1: Each thread sorts its local chunk using std::sort (e.g., IntroSort)
        if (start < end) {
            std::sort(data.begin() + start, data.begin() + end);
        }
        #pragma omp barrier // Ensure all chunks are sorted before merging starts
    }

    // Phase 2: Iterative parallel merge
    // Repeatedly merge adjacent sorted blocks.
    // merge_size goes from 1, 2, 4, 8, ... up to n/2
    for (int merge_size = 1; merge_size < n; merge_size *= 2) {
        std::vector<unsigned int>& src = in_data ? data : temp_buffer;
        std::vector<unsigned int>& dst = in_data ? temp_buffer : data;
        
        #pragma omp parallel for
        for (int i = 0; i < n; i += 2 * merge_size) {
            int start1 = i;
            int end1 = std::min(start1 + merge_size, n);
            int start2 = end1;
            int end2 = std::min(start2 + merge_size, n);
            
            // Only merge if there's a second block or first block is not fully merged
            if (start2 < end2 || start1 < end1) {
                std::merge(src.begin() + start1, src.begin() + end1,
                           src.begin() + start2, src.begin() + end2,
                           dst.begin() + start1);
            } else {
                // If only one block remains (e.g., at the very end of the array)
                // or if it's an empty merge, copy directly.
                // This handles cases where a block_size covers past 'n' or single element blocks.
                for (int k = start1; k < end1; ++k) {
                    dst[k] = src[k];
                }
            }
        }
        in_data = !in_data; // Toggle source/destination for the next merge pass
    }

    // If the final sorted data resides in the temp_buffer, copy it back to original 'data'
    if (!in_data) {
        #pragma omp parallel for
        for(int i = 0; i < n; ++i) {
            data[i] = temp_buffer[i];
        }
    }
}


// --- Scalable Parallel Radix Sort ---
// Implements a Counting Sort pass for one byte, using parallel prefix sum for efficient placement.
void scalable_radix_sort_pass(std::vector<unsigned int>& data, int byte_num) {
    int n = data.size();
    if (n == 0) return;

    std::vector<unsigned int> temp_buffer(n); // Buffer for sorted output of this pass
    int shift = byte_num * 8; // Bit shift to extract the relevant byte
    const int BUCKET_SIZE = 256; // 2^8 for a byte

    // Global counts array, shared across threads. Needs atomic updates or reduction.
    std::vector<int> global_counts(BUCKET_SIZE, 0); 
    
    #pragma omp parallel
    {
        int num_threads = omp_get_num_threads();
        int thread_id = omp_get_thread_num();

        // Step 1: Each thread builds a private histogram for its data chunk
        std::vector<int> local_counts(BUCKET_SIZE, 0);
        int chunk_size = (n + num_threads - 1) / num_threads;
        int start_idx = thread_id * chunk_size;
        int end_idx = std::min(start_idx + chunk_size, n);

        for (int i = start_idx; i < end_idx; ++i) {
            local_counts[(data[i] >> shift) & 0xFF]++;
        }

        // Step 2: Aggregate local counts into global_counts (critical section or atomic)
        #pragma omp critical
        {
            for (int i = 0; i < BUCKET_SIZE; ++i) {
                global_counts[i] += local_counts[i];
            }
        }
        #pragma omp barrier // Ensure all threads have updated global_counts

        // Step 3: Compute thread-private starting offsets for each bucket (exclusive scan for local elements)
        // This is the core of parallel placement without contention.
        // Each thread computes its contribution to the global offsets.
        std::vector<int> thread_start_offsets(BUCKET_SIZE); // Where THIS thread should start placing elements for each bucket
        
        #pragma omp single
        {
            // First, convert global_counts to an exclusive prefix sum to get final bucket start positions
            // This is done once by one thread
            int current_sum = 0;
            for (int i = 0; i < BUCKET_SIZE; ++i) {
                int count_for_bucket = global_counts[i];
                global_counts[i] = current_sum; // global_counts now stores global exclusive prefix sum
                current_sum += count_for_bucket;
            }
        }
        #pragma omp barrier // Ensure global_counts is a global exclusive prefix sum before threads proceed

        // Now, each thread figures out its specific starting point for placing elements
        // within each bucket, relative to the global starting point of that bucket.
        // This is done by performing a local prefix sum of previous threads' elements.
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            thread_start_offsets[i] = global_counts[i];
            // Accumulate counts from previous threads for this bucket
            for (int t = 0; t < thread_id; ++t) {
                int prev_thread_start_idx = t * chunk_size;
                int prev_thread_end_idx = std::min(prev_thread_start_idx + chunk_size, n);
                for (int k = prev_thread_start_idx; k < prev_thread_end_idx; ++k) {
                    if (((data[k] >> shift) & 0xFF) == i) {
                        thread_start_offsets[i]++;
                    }
                }
            }
        }

        // Step 4: Each thread places its elements into the temporary buffer in parallel
        for (int i = start_idx; i < end_idx; ++i) {
            int bucket = (data[i] >> shift) & 0xFF;
            temp_buffer[thread_start_offsets[bucket]++] = data[i];
        }
    }

    // Copy the sorted data back from the temporary buffer to the original array
    #pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        data[i] = temp_buffer[i];
    }
}

void scalable_radix_sort(std::vector<unsigned int>& data) {
    for (int i = 0; i < 4; ++i) { // 32-bit integers have 4 bytes
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
    
    omp_set_num_threads(threads); // Set the number of OpenMP threads

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

    bool sorted_correctly = is_sorted(data); // Verify correctness
    
    double duration_s = duration_ms.count() / 1000.0;
    // Calculate throughput in Mega-Keys per second
    double mkeys_per_second = (duration_s > 0) ? (static_cast<double>(N) / duration_s) / 1e6 : 0;

    json result = {
        {"algorithm", algorithm},
        {"N", N},
        {"threads", threads},
        {"time_ms", duration_ms.count()},
        {"mkeys_per_s", mkeys_per_second},
        {"correct", sorted_correctly}
    };
    std::cout << result.dump() << std::endl; // Output results as JSON
    
    return 0;
}