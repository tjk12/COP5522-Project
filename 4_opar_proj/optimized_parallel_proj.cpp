#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <cstring>
#include <mpi.h>
#include <omp.h>
#include "json.hpp"

// Check for GNU Parallel Mode
#if __has_include(<parallel/algorithm>)
#include <parallel/algorithm>
#define USE_GNU_PARALLEL
#endif

using json = nlohmann::json;

// --- Data Structures ---
struct Record {
    unsigned char data[100];

    // Compare first 10 bytes (Key)
    bool operator<(const Record& other) const {
        return std::memcmp(data, other.data, 10) < 0;
    }
    
    // Helper for Radix Sort
    unsigned char key_byte(int byte_index) const {
        return data[byte_index];
    }
};

// --- MPI Helpers for Large Messages (> 2GB) ---
// MPI uses int for counts, so we must chunk messages larger than 2GB.
const size_t MPI_MAX_BYTES = 2000000000L; // ~2GB safety limit

void safe_mpi_send(const std::vector<Record>& data, int dest, int tag, MPI_Comm comm) {
    size_t total_bytes = data.size() * sizeof(Record);
    size_t offset = 0;
    
    // Send total size first (as long long to be safe)
    unsigned long long size_u64 = total_bytes;
    MPI_Send(&size_u64, 1, MPI_UNSIGNED_LONG_LONG, dest, tag, comm);

    const char* ptr = reinterpret_cast<const char*>(data.data());
    while (offset < total_bytes) {
        size_t chunk_bytes = std::min(total_bytes - offset, MPI_MAX_BYTES);
        MPI_Send(ptr + offset, (int)chunk_bytes, MPI_BYTE, dest, tag, comm);
        offset += chunk_bytes;
    }
}

void safe_mpi_recv(std::vector<Record>& data, int src, int tag, MPI_Comm comm) {
    unsigned long long size_u64;
    MPI_Recv(&size_u64, 1, MPI_UNSIGNED_LONG_LONG, src, tag, comm, MPI_STATUS_IGNORE);
    
    size_t total_bytes = (size_t)size_u64;
    size_t num_records = total_bytes / sizeof(Record);
    data.resize(num_records);
    
    char* ptr = reinterpret_cast<char*>(data.data());
    size_t offset = 0;
    while (offset < total_bytes) {
        size_t chunk_bytes = std::min(total_bytes - offset, MPI_MAX_BYTES);
        MPI_Recv(ptr + offset, (int)chunk_bytes, MPI_BYTE, src, tag, comm, MPI_STATUS_IGNORE);
        offset += chunk_bytes;
    }
}

// --- Utility Functions ---
std::vector<Record> read_data(const std::string& filename, int rank) {
    std::vector<Record> data;
    if (rank == 0) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Error opening file: " << filename << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        const int RECORD_SIZE = 100;
        file.seekg(0, std::ios::end);
        long long file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        long long num_records = file_size / RECORD_SIZE;
        
        data.resize(num_records);
        
        char* buffer_ptr = reinterpret_cast<char*>(data.data());
        long long bytes_remaining = file_size;
        const long long CHUNK_SIZE = 1024 * 1024 * 1024; // 1 GB chunks

        while (bytes_remaining > 0) {
            long long bytes_to_read = std::min(bytes_remaining, CHUNK_SIZE);
            if (!file.read(buffer_ptr, bytes_to_read)) {
                std::cerr << "Error reading file." << std::endl;
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            buffer_ptr += bytes_to_read;
            bytes_remaining -= bytes_to_read;
        }
    }
    return data;
}

bool is_sorted(const std::vector<Record>& data) {
    // Parallel check
    if (data.empty()) return true;
    bool sorted = true;
    #pragma omp parallel for reduction(&&:sorted)
    for (size_t i = 0; i < data.size() - 1; ++i) {
        if (data[i+1] < data[i]) {
            sorted = false;
        }
    }
    return sorted;
}

// --- Local Sorting Algorithms ---

// Parallel LSD Radix Sort (In-Memory)
void local_radix_sort(std::vector<Record>& data) {
    if (data.empty()) return;
    size_t n = data.size();
    std::vector<Record> buffer(n);
    bool in_data = true; // true if data is in 'data', false if in 'buffer'
    
    int num_threads = omp_get_max_threads();

    for (int byte_idx = 9; byte_idx >= 0; --byte_idx) {
        // 1. Compute Histograms per thread
        std::vector<int> all_hists(num_threads * 256, 0);
        
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            int* my_hist = &all_hists[tid * 256];
            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; ++i) {
                const Record& r = in_data ? data[i] : buffer[i];
                my_hist[r.key_byte(byte_idx)]++;
            }
        }
        
        // 2. Compute Global Offsets (Prefix Sum)
        // offsets[bucket][thread] -> where thread T should start writing for bucket B
        std::vector<int> offsets(256 * num_threads);
        int current_offset = 0;
        for (int b = 0; b < 256; ++b) {
            for (int t = 0; t < num_threads; ++t) {
                offsets[b * num_threads + t] = current_offset;
                current_offset += all_hists[t * 256 + b];
            }
        }
        
        // 3. Shuffle / Scatter
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            // Local copy of offsets for this thread
            std::vector<int> local_offsets(256);
            for(int b=0; b<256; ++b) {
                local_offsets[b] = offsets[b * num_threads + tid];
            }
            
            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; ++i) {
                const Record& src = in_data ? data[i] : buffer[i];
                int b = src.key_byte(byte_idx);
                int dst_idx = local_offsets[b]++;
                if (in_data) buffer[dst_idx] = src;
                else data[dst_idx] = src;
            }
        }
        
        in_data = !in_data;
    }
    
    if (!in_data) {
        data = buffer;
    }
}

// --- Optimized Merge Sort ---
void merge_sort(std::vector<Record>& data, int rank, int world_size) {
    // 1. Scatter
    // We use a simplified scatter: Rank 0 sends chunks to others.
    // To handle >2GB, we use safe_mpi_send/recv.
    
    size_t n_global = 0;
    if (rank == 0) n_global = data.size();
    MPI_Bcast(&n_global, sizeof(size_t), MPI_BYTE, 0, MPI_COMM_WORLD);
    
    size_t chunk_size = n_global / world_size;
    size_t remainder = n_global % world_size;
    
    std::vector<Record> local_data;
    
    if (rank == 0) {
        // Send to others
        size_t offset = 0;
        for (int i = 0; i < world_size; ++i) {
            size_t count = chunk_size + (i < remainder ? 1 : 0);
            if (i == 0) {
                // Keep local
                local_data.assign(data.begin(), data.begin() + count);
            } else {
                // Send subset
                // Creating a temp vector is memory inefficient but safe.
                // For better memory, we could send directly from data pointer.
                // But safe_mpi_send takes vector. Let's overload or just use pointer logic inside.
                // For simplicity/safety with the existing helper:
                std::vector<Record> temp_chunk(data.begin() + offset, data.begin() + offset + count);
                safe_mpi_send(temp_chunk, i, 0, MPI_COMM_WORLD);
            }
            offset += count;
        }
        // Free global data memory on rank 0 to save RAM during merge
        std::vector<Record>().swap(data); 
    } else {
        safe_mpi_recv(local_data, 0, 0, MPI_COMM_WORLD);
    }
    
    // 2. Local Sort
#ifdef USE_GNU_PARALLEL
    __gnu_parallel::sort(local_data.begin(), local_data.end());
#else
    // Fallback to standard sort (serial) or write a custom parallel merge sort.
    // Given the constraints, std::sort is robust, but slow.
    // Let's use a simple OpenMP sort if GNU parallel is missing.
    // But for now, std::sort is the baseline fallback.
    std::sort(local_data.begin(), local_data.end());
#endif

    // 3. Tree Merge
    for (int step = 1; step < world_size; step *= 2) {
        if (rank % (2 * step) != 0) {
            int dest = rank - step;
            safe_mpi_send(local_data, dest, 1, MPI_COMM_WORLD);
            break;
        } else if (rank + step < world_size) {
            int src = rank + step;
            std::vector<Record> received_data;
            safe_mpi_recv(received_data, src, 1, MPI_COMM_WORLD);
            
            std::vector<Record> merged_data(local_data.size() + received_data.size());
#ifdef USE_GNU_PARALLEL
            __gnu_parallel::merge(local_data.begin(), local_data.end(),
                                  received_data.begin(), received_data.end(),
                                  merged_data.begin());
#else
            std::merge(local_data.begin(), local_data.end(),
                       received_data.begin(), received_data.end(),
                       merged_data.begin());
#endif
            local_data = std::move(merged_data);
        }
    }
    
    if (rank == 0) {
        data = std::move(local_data);
    }
}

// --- Optimized Radix Sort (MSD Distribution + Local LSD Sort) ---
void radix_sort(std::vector<Record>& data, int rank, int world_size) {
    // 1. Partitioning (MSD - First Byte)
    // Determine which keys go to which processor.
    // We split the 256 possible values of the first byte among processors.
    
    // Read data on Rank 0
    // (Already done in main, passed in 'data')
    
    // Broadcast global N
    size_t n_global = 0;
    if (rank == 0) n_global = data.size();
    MPI_Bcast(&n_global, sizeof(size_t), MPI_BYTE, 0, MPI_COMM_WORLD);
    
    // Define splitters: Uniform distribution of the 256 buckets
    // Rank i handles buckets [start_b, end_b)
    int buckets_per_proc = 256 / world_size;
    int remainder = 256 % world_size;
    
    // Helper to get owner of a bucket
    auto get_owner = [&](int bucket) {
        int r = 0;
        int current_b = 0;
        for(int i=0; i<world_size; ++i) {
            int count = buckets_per_proc + (i < remainder ? 1 : 0);
            if (bucket < current_b + count) return i;
            current_b += count;
        }
        return world_size - 1;
    };

    // 2. Distribute Data
    // Rank 0 has all data. It buckets and sends.
    // Other ranks receive.
    // NOTE: This assumes we start with data on Rank 0. 
    // If we wanted fully distributed start, we'd do Alltoall.
    // But the interface assumes Rank 0 loads data.
    
    std::vector<Record> local_data;
    
    if (rank == 0) {
        // Bucket data
        std::vector<std::vector<Record>> send_buffers(world_size);
        // Pre-allocate to avoid reallocs? Hard to guess sizes.
        // Just reserve average.
        for(auto& buf : send_buffers) buf.reserve(n_global / world_size * 1.2);
        
        for (const auto& r : data) {
            int b = r.key_byte(0); // MSB
            int dest = get_owner(b);
            send_buffers[dest].push_back(r);
        }
        
        // Free original data
        std::vector<Record>().swap(data);
        
        // Send
        for (int i = 0; i < world_size; ++i) {
            if (i == rank) {
                local_data = std::move(send_buffers[i]);
            } else {
                safe_mpi_send(send_buffers[i], i, 2, MPI_COMM_WORLD);
                // Free memory immediately
                std::vector<Record>().swap(send_buffers[i]);
            }
        }
    } else {
        safe_mpi_recv(local_data, 0, 2, MPI_COMM_WORLD);
    }
    
    // 3. Local Sort
    // Use our optimized in-memory LSD Radix Sort
    local_radix_sort(local_data);
    
    // 4. Gather
    // Since we used MSD partitioning, the processors are ordered.
    // Rank 0 < Rank 1 < ...
    // We just need to gather them in order.
    
    if (rank == 0) {
        data.reserve(n_global);
        // Copy own data
        data.insert(data.end(), local_data.begin(), local_data.end());
        
        // Receive from others
        for (int i = 1; i < world_size; ++i) {
            std::vector<Record> chunk;
            safe_mpi_recv(chunk, i, 3, MPI_COMM_WORLD);
            data.insert(data.end(), chunk.begin(), chunk.end());
        }
    } else {
        safe_mpi_send(local_data, 0, 3, MPI_COMM_WORLD);
    }
}

// --- Main Driver ---
int main(int argc, char* argv[]) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    
    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 4) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <algorithm> <threads_per_process> <filename>" << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    std::string algorithm = argv[1];
    int threads = std::stoi(argv[2]);
    std::string filename = argv[3];
    
    omp_set_num_threads(threads);

    // Read data (Rank 0 only)
    auto data = read_data(filename, rank);
    size_t N = 0;
    if (rank == 0) N = data.size();

    MPI_Barrier(MPI_COMM_WORLD);
    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "merge_sort") {
        merge_sort(data, rank, world_size);
    } else if (algorithm == "radix_sort") {
        radix_sort(data, rank, world_size);
    } else {
        if (rank == 0) std::cerr << "Unknown algorithm" << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    auto end = std::chrono::high_resolution_clock::now();

    if (rank == 0) {
        std::chrono::duration<double, std::milli> duration_ms = end - start;
        bool sorted_correctly = is_sorted(data);
        
        double duration_s = duration_ms.count() / 1000.0;
        double mkeys_per_second = (duration_s > 0) ? (N / 1e6 / duration_s) : 0;

        json result = {
            {"Title", "Optimized Parallel Sorter"},
            {"algorithm", algorithm},
            {"N", N},
            {"mpi_procs", world_size},
            {"omp_threads", threads},
            {"threads", world_size * threads},
            {"time_ms", duration_ms.count()},
            {"mkeys_per_s", mkeys_per_second},
            {"correct", sorted_correctly}
        };
        std::cout << result.dump() << std::endl;
    }

    MPI_Finalize();
    return 0;
}