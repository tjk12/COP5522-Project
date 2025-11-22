#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <cstring> // For std::memcmp
#include <mpi.h>
#include <omp.h>
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
        
        // Read in chunks to avoid issues with reading > 2GB/4GB in a single call on Windows
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
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i+1] < data[i]) {
            return false;
        }
    }
    return true;
}

// --- Optimized Hybrid MPI+OpenMP Merge Sort ---
void merge_sort(std::vector<Record>& data, int rank, int world_size) {
    // Ensure data is only non-empty on rank 0
    if (rank != 0) {
        data.clear();
    }
    
    int n_global = 0;
    if (rank == 0) n_global = data.size();
    MPI_Bcast(&n_global, 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> sendcounts(world_size);
    std::vector<int> displs(world_size, 0);
    int chunk_size = n_global / world_size;
    int remainder = n_global % world_size;
    for (int i = 0; i < world_size; ++i) {
        sendcounts[i] = (chunk_size + (i < remainder ? 1 : 0)) * sizeof(Record);
        if (i > 0) displs[i] = displs[i - 1] + sendcounts[i - 1];
    }

    int local_bytes = sendcounts[rank];
    int local_count = local_bytes / sizeof(Record);
    std::vector<Record> local_data(local_count);
    
    // Ensure send buffer is valid on rank 0
    if (rank == 0 && data.empty()) {
        data.resize(n_global);
    }
    
    MPI_Scatterv(rank == 0 ? data.data() : nullptr, sendcounts.data(), displs.data(), MPI_BYTE,
                 local_data.data(), local_bytes, MPI_BYTE, 0, MPI_COMM_WORLD);

    // Step 2: Each process uses OpenMP to sort its local data
    int n_local = local_data.size();
    if (n_local > 0) {
        std::vector<Record> temp_buffer(n_local);
        bool in_data = true;
        int initial_merge_size = 1;

        // Each thread sorts a statically-assigned chunk of the local data.
        #pragma omp parallel
        {
            int num_threads = omp_get_num_threads();
            int thread_id = omp_get_thread_num();
            int merge_size = (n_local + num_threads - 1) / num_threads;
            int start = thread_id * merge_size;
            int end = std::min(start + merge_size, n_local);
            if (start < end) std::sort(local_data.begin() + start, local_data.begin() + end);
            #pragma omp barrier
            #pragma omp single
            {
                initial_merge_size = merge_size;
            }
        }

        // The merge starts from the size of the blocks we just sorted.
        for (int merge_size = initial_merge_size; merge_size < n_local; merge_size *= 2) {
            std::vector<Record>& src = in_data ? local_data : temp_buffer;
            std::vector<Record>& dst = in_data ? temp_buffer : local_data;
            #pragma omp parallel for
            for (int i = 0; i < n_local; i += 2 * merge_size) {
                int start1 = i;
                int end1 = std::min(start1 + merge_size, n_local);
                int start2 = end1;
                int end2 = std::min(start2 + merge_size, n_local);
                std::merge(src.begin() + start1, src.begin() + end1,
                           src.begin() + start2, src.begin() + end2,
                           dst.begin() + start1);
            }
            in_data = !in_data;
        }

        if (!in_data) {
            #pragma omp parallel for
            for(int i=0; i<n_local; ++i) {
                local_data[i] = temp_buffer[i];
            }
        }
    }

    // Step 3: Iterative tree-based merge between MPI processes
    for (int step = 1; step < world_size; step *= 2) {
        if (rank % (2 * step) != 0) {
            int dest = rank - step;
            int size_bytes = local_data.size() * sizeof(Record);
            MPI_Send(&size_bytes, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
            MPI_Send(local_data.data(), size_bytes, MPI_BYTE, dest, 1, MPI_COMM_WORLD);
            break;
        } else if (rank + step < world_size) {
            int src = rank + step;
            int received_bytes;
            MPI_Recv(&received_bytes, 1, MPI_INT, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            int received_count = received_bytes / sizeof(Record);
            std::vector<Record> received_data(received_count);
            MPI_Recv(received_data.data(), received_bytes, MPI_BYTE, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<Record> merged_data(local_data.size() + received_count);
            std::merge(local_data.begin(), local_data.end(), received_data.begin(), received_data.end(), merged_data.begin());
            local_data = merged_data;
        }
    }

    if (rank == 0) {
        data = local_data;
    }
}

// --- Optimized Hybrid MPI+OpenMP Radix Sort ---
void radix_sort_pass(std::vector<Record>& local_data, int byte_num, int rank, int world_size) {
    int n_local = local_data.size();
    if (n_local == 0 && world_size == 1) return;

    const int BUCKET_SIZE = 256;
    int num_threads = omp_get_max_threads();

    // Step 1: Parallel local histogram (OpenMP)
    // Use a single contiguous allocation for better cache performance.
    std::vector<int> all_histograms(num_threads * BUCKET_SIZE, 0);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_local; ++i) {
        int thread_id = omp_get_thread_num();
        all_histograms[thread_id * BUCKET_SIZE + local_data[i].key_byte(byte_num)]++;
    }

    // Reduce the thread-local histograms into a single process-local histogram.
    std::vector<int> process_local_hist(BUCKET_SIZE, 0);
    #pragma omp parallel for
    for (int bucket = 0; bucket < BUCKET_SIZE; ++bucket) {
        for (int t = 0; t < num_threads; ++t) {
            process_local_hist[bucket] += all_histograms[t * BUCKET_SIZE + bucket];
        }
    }

    // Step 2: Global histogram via MPI_Allreduce
    std::vector<int> global_hist(BUCKET_SIZE, 0);
    MPI_Allreduce(process_local_hist.data(), global_hist.data(), BUCKET_SIZE, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    // Step 3: Determine send/recv counts for MPI_Alltoallv
    std::vector<int> to_send_counts(world_size, 0);
    std::vector<std::vector<Record>> to_send_buckets(world_size);
    int buckets_per_proc = (BUCKET_SIZE + world_size - 1) / world_size;
    
    for (const auto& val : local_data) {
        int bucket_idx = val.key_byte(byte_num);
        int dest_proc = std::min(bucket_idx / buckets_per_proc, world_size - 1);
        to_send_buckets[dest_proc].push_back(val);
    }
    
    // We need to send counts in BYTES for Alltoallv later, but Alltoall expects counts of INTEGERS
    // So we exchange the number of RECORDS first
    std::vector<int> to_send_num_records(world_size);
    for (int p = 0; p < world_size; ++p) {
        to_send_num_records[p] = to_send_buckets[p].size();
    }

    std::vector<int> to_recv_num_records(world_size, 0);
    MPI_Alltoall(to_send_num_records.data(), 1, MPI_INT, to_recv_num_records.data(), 1, MPI_INT, MPI_COMM_WORLD);

    // Step 4: Exchange data with MPI_Alltoallv
    std::vector<int> send_bytes(world_size), recv_bytes(world_size);
    std::vector<int> sdispls(world_size, 0), rdispls(world_size, 0);
    
    std::vector<Record> send_buf;
    send_buf.reserve(n_local);
    
    for (int p = 0; p < world_size; ++p) {
        send_bytes[p] = to_send_num_records[p] * sizeof(Record);
        recv_bytes[p] = to_recv_num_records[p] * sizeof(Record);
        
        if (p > 0) {
            sdispls[p] = sdispls[p - 1] + send_bytes[p - 1];
            rdispls[p] = rdispls[p - 1] + recv_bytes[p - 1];
        }
        send_buf.insert(send_buf.end(), to_send_buckets[p].begin(), to_send_buckets[p].end());
    }
    
    int total_recv_bytes = 0;
    for (int p = 0; p < world_size; ++p) {
        total_recv_bytes += recv_bytes[p];
    }
    
    std::vector<Record> recv_buf(total_recv_bytes / sizeof(Record));
    
    MPI_Alltoallv(send_buf.data(), send_bytes.data(), sdispls.data(), MPI_BYTE,
                  recv_buf.data(), recv_bytes.data(), rdispls.data(), MPI_BYTE,
                  MPI_COMM_WORLD);

    // Step 5: Parallel local sort/placement of received data (OpenMP)
    local_data = recv_buf;
    int n_received = local_data.size();
    if (n_received > 0) {
        std::vector<Record> temp_buffer(n_received);
        bool in_data = true;
        int initial_merge_size = 1;
        #pragma omp parallel
        {
            int num_threads = omp_get_num_threads();
            int thread_id = omp_get_thread_num();
            int merge_size = (n_received + num_threads - 1) / num_threads;
            int start = thread_id * merge_size;
            int end = std::min(start + merge_size, n_received);
            if (start < end) std::sort(local_data.begin() + start, local_data.begin() + end);
            #pragma omp barrier
            #pragma omp single
            {
                initial_merge_size = merge_size;
            }
        }
        for (int merge_size = initial_merge_size; merge_size < n_received; merge_size *= 2) {
            auto& src = in_data ? local_data : temp_buffer;
            auto& dst = in_data ? temp_buffer : local_data;
            #pragma omp parallel for
            for (int i = 0; i < n_received; i += 2 * merge_size) {
                std::merge(src.begin() + i, src.begin() + std::min(i + merge_size, n_received),
                           src.begin() + std::min(i + merge_size, n_received), src.begin() + std::min(i + 2 * merge_size, n_received),
                           dst.begin() + i);
            }
            in_data = !in_data;
        }
        if (!in_data) {
            #pragma omp parallel for
            for(int i=0; i<n_received; ++i) {
                local_data[i] = temp_buffer[i];
            }
        }
    }
}

void radix_sort(std::vector<Record>& data, int rank, int world_size) {
    // Ensure data is only non-empty on rank 0
    if (rank != 0) {
        data.clear();
    }
    
    int n_global = 0;
    if (rank == 0) n_global = data.size();
    MPI_Bcast(&n_global, 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> sendcounts(world_size);
    std::vector<int> displs(world_size, 0);
    int chunk_size = n_global / world_size;
    int remainder = n_global % world_size;
    for (int i = 0; i < world_size; ++i) {
        sendcounts[i] = (chunk_size + (i < remainder ? 1 : 0)) * sizeof(Record);
        if (i > 0) displs[i] = displs[i - 1] + sendcounts[i - 1];
    }

    int local_bytes = sendcounts[rank];
    int local_count = local_bytes / sizeof(Record);
    std::vector<Record> local_data(local_count);
    
    // Ensure send buffer is valid on rank 0
    if (rank == 0 && data.empty()) {
        data.resize(n_global);
    }
    MPI_Scatterv(rank == 0 ? data.data() : nullptr, sendcounts.data(), displs.data(), MPI_BYTE,
                 local_data.data(), local_bytes, MPI_BYTE, 0, MPI_COMM_WORLD);

    // 10 passes for 10-byte keys
    for (int i = 9; i >= 0; --i) {
        radix_sort_pass(local_data, i, rank, world_size);
    }

    // Gather all sorted parts to the root process
    int local_size_bytes = local_data.size() * sizeof(Record);
    std::vector<int> recvcounts(world_size);
    MPI_Gather(&local_size_bytes, 1, MPI_INT, recvcounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        displs[0] = 0;
        for (int i = 1; i < world_size; i++) {
            displs[i] = displs[i - 1] + recvcounts[i - 1];
        }
        data.resize(n_global);
    }

    MPI_Gatherv(local_data.data(), local_size_bytes, MPI_BYTE,
                rank == 0 ? data.data() : nullptr, recvcounts.data(), displs.data(), MPI_BYTE,
                0, MPI_COMM_WORLD);
}

// --- Main Driver ---
int main(int argc, char* argv[]) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        std::cerr << "MPI does not provide the required thread support." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 4) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <algorithm> <threads_per_process> <filename>" << std::endl;
            std::cerr << "Algorithms: merge_sort, radix_sort" << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    std::string algorithm = argv[1];
    int threads = std::stoi(argv[2]);
    std::string filename = argv[3];
    
    omp_set_num_threads(threads);

    auto data = read_data(filename, rank);
    size_t N = data.size(); // N is only correct on rank 0 initially

    MPI_Barrier(MPI_COMM_WORLD);
    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "merge_sort") {
        merge_sort(data, rank, world_size);
    } else if (algorithm == "radix_sort") {
        radix_sort(data, rank, world_size);
    } else {
        if (rank == 0) {
            std::cerr << "Unknown algorithm: " << algorithm << std::endl;
        }
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