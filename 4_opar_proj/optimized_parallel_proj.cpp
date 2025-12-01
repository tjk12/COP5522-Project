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
#include <climits>
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
    
    // Send total size first (as long long to be safe)
    unsigned long long size_u64 = total_bytes;
    MPI_Send(&size_u64, 1, MPI_UNSIGNED_LONG_LONG, dest, tag, comm);

    const char* ptr = reinterpret_cast<const char*>(data.data());
    size_t offset = 0;
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

// Parallel Read: Each rank reads its own chunk of the file
std::vector<Record> read_data_parallel(const std::string& filename, int rank, int world_size) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        if (rank == 0) std::cerr << "Error opening file: " << filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    const int RECORD_SIZE = 100;
    file.seekg(0, std::ios::end);
    long long file_size = file.tellg();
    long long total_records = file_size / RECORD_SIZE;

    // Calculate chunk for this rank
    long long records_per_proc = total_records / world_size;
    long long remainder = total_records % world_size;
    
    long long my_start_idx = rank * records_per_proc + std::min((long long)rank, remainder);
    long long my_count = records_per_proc + (rank < remainder ? 1 : 0);

    std::vector<Record> data(my_count);
    
    // Seek to start position
    file.seekg(my_start_idx * RECORD_SIZE, std::ios::beg);
    
    // Read in chunks (1GB)
    char* buffer_ptr = reinterpret_cast<char*>(data.data());
    long long bytes_remaining = my_count * RECORD_SIZE;
    const long long CHUNK_SIZE = 1024 * 1024 * 1024; 

    while (bytes_remaining > 0) {
        long long bytes_to_read = std::min(bytes_remaining, CHUNK_SIZE);
        if (!file.read(buffer_ptr, bytes_to_read)) {
            std::cerr << "Error reading file on rank " << rank << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        buffer_ptr += bytes_to_read;
        bytes_remaining -= bytes_to_read;
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
    // 1. Local Sort (Data is already distributed by parallel read)
#ifdef USE_GNU_PARALLEL
    __gnu_parallel::sort(data.begin(), data.end());
#else
    std::sort(data.begin(), data.end());
#endif

    // 2. Tree Merge
    for (int step = 1; step < world_size; step *= 2) {
        if (rank % (2 * step) != 0) {
            int dest = rank - step;
            safe_mpi_send(data, dest, 1, MPI_COMM_WORLD);
            break;
        } else if (rank + step < world_size) {
            int src = rank + step;
            std::vector<Record> received_data;
            safe_mpi_recv(received_data, src, 1, MPI_COMM_WORLD);
            
            std::vector<Record> merged_data(data.size() + received_data.size());
#ifdef USE_GNU_PARALLEL
            __gnu_parallel::merge(data.begin(), data.end(),
                                  received_data.begin(), received_data.end(),
                                  merged_data.begin());
#else
            std::merge(data.begin(), data.end(),
                       received_data.begin(), received_data.end(),
                       merged_data.begin());
#endif
            data = std::move(merged_data);
        }
    }
}

// --- Optimized Radix Sort (Distributed Partitioning) ---
void radix_sort(std::vector<Record>& data, int rank, int world_size) {
    // 1. Local Histogram (MSD - First Byte)
    // Count how many keys belong to each destination rank
    
    int buckets_per_proc = 256 / world_size;
    int remainder = 256 % world_size;
    
    // Helper to get owner of a bucket (byte value 0-255)
    auto get_owner = [&](int bucket) {
        int current_b = 0;
        for(int i=0; i<world_size; ++i) {
            int count = buckets_per_proc + (i < remainder ? 1 : 0);
            if (bucket < current_b + count) return i;
            current_b += count;
        }
        return world_size - 1;
    };

    int num_threads = omp_get_max_threads();
    std::vector<long long> send_counts(world_size, 0);
    
    // Thread-local counts
    std::vector<std::vector<long long>> thread_counts(num_threads, std::vector<long long>(world_size, 0));

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        #pragma omp for schedule(static)
        for (size_t i = 0; i < data.size(); ++i) {
            int b = data[i].key_byte(0);
            int dest = get_owner(b);
            thread_counts[tid][dest]++;
        }
    }
    
    // Aggregate counts
    for (int t = 0; t < num_threads; ++t) {
        for (int r = 0; r < world_size; ++r) {
            send_counts[r] += thread_counts[t][r];
        }
    }

    // 2. Alltoall to exchange counts
    std::vector<long long> recv_counts(world_size);
    MPI_Alltoall(send_counts.data(), 1, MPI_LONG_LONG, 
                 recv_counts.data(), 1, MPI_LONG_LONG, MPI_COMM_WORLD);

    // 3. Calculate Offsets for Alltoallv
    std::vector<int> send_displs(world_size, 0);
    std::vector<int> recv_displs(world_size, 0);
    std::vector<int> send_counts_int(world_size);
    std::vector<int> recv_counts_int(world_size);
    
    long long total_send = 0;
    long long total_recv = 0;
    bool use_alltoallv = true;  // Flag to check if we can safely use MPI_Alltoallv

    for (int i = 0; i < world_size; ++i) {
        long long send_bytes = send_counts[i] * sizeof(Record);
        long long recv_bytes = recv_counts[i] * sizeof(Record);
        long long send_disp_bytes = total_send * sizeof(Record);
        long long recv_disp_bytes = total_recv * sizeof(Record);
        
        // Check for int overflow (MPI_Alltoallv uses int for counts/displs)
        // INT_MAX is approximately 2.1GB
        if (send_bytes > INT_MAX || recv_bytes > INT_MAX ||
            send_disp_bytes > INT_MAX || recv_disp_bytes > INT_MAX) {
            use_alltoallv = false;
        }
        
        send_displs[i] = (int)send_disp_bytes;
        send_counts_int[i] = (int)send_bytes;
        total_send += send_counts[i];

        recv_displs[i] = (int)recv_disp_bytes;
        recv_counts_int[i] = (int)recv_bytes;
        total_recv += recv_counts[i];
    }

    // 4. Shuffle Data Locally to prepare for Send
    std::vector<Record> send_buffer(data.size());
    std::vector<size_t> offsets(world_size, 0);
    
    // Calculate local offsets for shuffling
    size_t current = 0;
    for(int i=0; i<world_size; ++i) {
        offsets[i] = current;
        current += send_counts[i];
    }
    
    // Thread-safe shuffling using atomic offsets or pre-calculated thread offsets
    // Using pre-calculated thread offsets for speed
    std::vector<std::vector<size_t>> thread_offsets(num_threads, std::vector<size_t>(world_size));
    std::vector<size_t> running_offsets = offsets;
    
    for(int t=0; t<num_threads; ++t) {
        for(int r=0; r<world_size; ++r) {
            thread_offsets[t][r] = running_offsets[r];
            running_offsets[r] += thread_counts[t][r];
        }
    }

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        #pragma omp for schedule(static)
        for (size_t i = 0; i < data.size(); ++i) {
            int b = data[i].key_byte(0);
            int dest = get_owner(b);
            size_t idx = thread_offsets[tid][dest]++;
            send_buffer[idx] = data[i];
        }
    }
    
    // Free original data
    std::vector<Record>().swap(data);

    // 5. Exchange Data
    std::vector<Record> recv_buffer(total_recv);
    
    if (use_alltoallv) {
        // Fast path: Use MPI_Alltoallv for smaller datasets
        MPI_Alltoallv(send_buffer.data(), send_counts_int.data(), send_displs.data(), MPI_BYTE,
                      recv_buffer.data(), recv_counts_int.data(), recv_displs.data(), MPI_BYTE,
                      MPI_COMM_WORLD);
    } else {
        // Fallback: Manual point-to-point communication for large datasets
        // This avoids INT_MAX overflow in MPI_Alltoallv
        
        // Prepare send partitions
        std::vector<std::vector<Record>> send_partitions(world_size);
        for (int dest = 0; dest < world_size; ++dest) {
            if (send_counts[dest] > 0) {
                size_t offset = (dest == 0) ? 0 : (send_displs[dest] / sizeof(Record));
                send_partitions[dest].assign(
                    send_buffer.begin() + offset,
                    send_buffer.begin() + offset + send_counts[dest]
                );
            }
        }
        
        // Free send_buffer to save memory
        std::vector<Record>().swap(send_buffer);
        
        // Send data to all ranks (including self-copy)
        for (int dest = 0; dest < world_size; ++dest) {
            if (send_counts[dest] > 0) {
                if (dest == rank) {
                    // Self-copy
                    std::copy(send_partitions[dest].begin(), send_partitions[dest].end(),
                             recv_buffer.begin() + (recv_displs[rank] / sizeof(Record)));
                } else {
                    // Use safe_mpi_send for large messages
                    safe_mpi_send(send_partitions[dest], dest, 100 + rank, MPI_COMM_WORLD);
                }
            }
        }
        
        // Receive data from all ranks
        size_t recv_offset = 0;
        for (int src = 0; src < world_size; ++src) {
            if (recv_counts[src] > 0 && src != rank) {
                std::vector<Record> temp;
                safe_mpi_recv(temp, src, 100 + src, MPI_COMM_WORLD);
                std::copy(temp.begin(), temp.end(), recv_buffer.begin() + recv_offset);
            }
            recv_offset += recv_counts[src];
        }
    }

    // 6. Local Sort
    local_radix_sort(recv_buffer);
    
    // 7. Gather (Optional - only if we need everything on Rank 0)
    // For benchmarking, we usually stop here or do a parallel write.
    // To match previous behavior, we gather to Rank 0.
    
    // Gather counts
    long long my_count = recv_buffer.size();
    std::vector<long long> all_counts(world_size);
    MPI_Gather(&my_count, 1, MPI_LONG_LONG, all_counts.data(), 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        long long total_final = 0;
        for(auto c : all_counts) total_final += c;
        data.resize(total_final);
        
        // Copy rank 0's data
        std::copy(recv_buffer.begin(), recv_buffer.end(), data.begin());
        
        // Receive from others
        size_t current_offset = recv_buffer.size();
        for(int i=1; i<world_size; ++i) {
            if (all_counts[i] > 0) {
                // Receive directly into the final vector
                // Need to use safe_recv logic because these chunks can be huge
                std::vector<Record> chunk;
                safe_mpi_recv(chunk, i, 99, MPI_COMM_WORLD);
                std::copy(chunk.begin(), chunk.end(), data.begin() + current_offset);
                current_offset += chunk.size();
            }
        }
    } else {
        if (my_count > 0) {
            safe_mpi_send(recv_buffer, 0, 99, MPI_COMM_WORLD);
        }
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

    // Parallel Read
    auto data = read_data_parallel(filename, rank, world_size);
    
    // Determine total N for reporting
    long long local_N = data.size();
    long long global_N = 0;
    MPI_Reduce(&local_N, &global_N, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

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
        double mkeys_per_second = (duration_s > 0) ? (global_N / 1e6 / duration_s) : 0;

        json result = {
            {"Title", "Optimized Parallel Sorter"},
            {"algorithm", algorithm},
            {"N", global_N},
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