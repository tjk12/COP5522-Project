#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <mpi.h>
#include <omp.h>
#include "json.hpp"

using json = nlohmann::json;

// --- Utility Functions ---
std::vector<unsigned int> read_data(const std::string& filename, int rank) {
    std::vector<unsigned int> data;
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
        data.reserve(num_records);
        char buffer[RECORD_SIZE];
        for (long long i = 0; i < num_records; ++i) {
            if (!file.read(buffer, RECORD_SIZE)) {
                std::cerr << "Error reading record " << i << " from file." << std::endl;
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            data.push_back(*reinterpret_cast<unsigned int*>(buffer));
        }
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

// --- Optimized Hybrid MPI+OpenMP Merge Sort ---
void merge_sort(std::vector<unsigned int>& data, int rank, int world_size) {
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
        sendcounts[i] = chunk_size + (i < remainder ? 1 : 0);
        if (i > 0) displs[i] = displs[i - 1] + sendcounts[i - 1];
    }

    std::vector<unsigned int> local_data(sendcounts[rank]);
    // Ensure send buffer is valid on rank 0
    if (rank == 0 && data.empty()) {
        data.resize(n_global);
    }
    MPI_Scatterv(rank == 0 ? data.data() : nullptr, sendcounts.data(), displs.data(), MPI_UNSIGNED,
                 local_data.data(), sendcounts[rank], MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    // Step 2: Each process uses OpenMP to sort its local data
    int n_local = local_data.size();
    if (n_local > 0) {
        std::vector<unsigned int> temp_buffer(n_local);
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
            std::vector<unsigned int>& src = in_data ? local_data : temp_buffer;
            std::vector<unsigned int>& dst = in_data ? temp_buffer : local_data;
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
            int size = local_data.size();
            MPI_Send(&size, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
            MPI_Send(local_data.data(), size, MPI_UNSIGNED, dest, 1, MPI_COMM_WORLD);
            break;
        } else if (rank + step < world_size) {
            int src = rank + step;
            int received_size;
            MPI_Recv(&received_size, 1, MPI_INT, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<unsigned int> received_data(received_size);
            MPI_Recv(received_data.data(), received_size, MPI_UNSIGNED, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<unsigned int> merged_data(local_data.size() + received_size);
            std::merge(local_data.begin(), local_data.end(), received_data.begin(), received_data.end(), merged_data.begin());
            local_data = merged_data;
        }
    }

    if (rank == 0) {
        data = local_data;
    }
}

// --- Optimized Hybrid MPI+OpenMP Radix Sort ---
void radix_sort_pass(std::vector<unsigned int>& local_data, int byte_num, int rank, int world_size) {
    int n_local = local_data.size();
    if (n_local == 0 && world_size == 1) return;

    int shift = byte_num * 8;
    const int BUCKET_SIZE = 256;
    int num_threads = omp_get_max_threads();

    // Step 1: Parallel local histogram (OpenMP)
    // Use a single contiguous allocation for better cache performance.
    std::vector<int> all_histograms(num_threads * BUCKET_SIZE, 0);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_local; ++i) {
        int thread_id = omp_get_thread_num();
        all_histograms[thread_id * BUCKET_SIZE + ((local_data[i] >> shift) & 0xFF)]++;
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
    std::vector<std::vector<unsigned int>> to_send_buckets(world_size);
    int buckets_per_proc = (BUCKET_SIZE + world_size - 1) / world_size;
    for (const auto& val : local_data) {
        int bucket_idx = (val >> shift) & 0xFF;
        int dest_proc = std::min(bucket_idx / buckets_per_proc, world_size - 1);
        to_send_buckets[dest_proc].push_back(val);
    }
    for (int p = 0; p < world_size; ++p) {
        to_send_counts[p] = to_send_buckets[p].size();
    }

    std::vector<int> to_recv_counts(world_size, 0);
    MPI_Alltoall(to_send_counts.data(), 1, MPI_INT, to_recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);

    // Step 4: Exchange data with MPI_Alltoallv
    std::vector<int> sdispls(world_size, 0), rdispls(world_size, 0);
    std::vector<unsigned int> send_buf;
    send_buf.reserve(n_local);
    for (int p = 0; p < world_size; ++p) {
        if (p > 0) sdispls[p] = sdispls[p - 1] + to_send_counts[p - 1];
        send_buf.insert(send_buf.end(), to_send_buckets[p].begin(), to_send_buckets[p].end());
    }
    int total_recv = 0;
    for (int p = 0; p < world_size; ++p) {
        if (p > 0) rdispls[p] = rdispls[p - 1] + to_recv_counts[p - 1];
        total_recv += to_recv_counts[p];
    }
    std::vector<unsigned int> recv_buf(total_recv);
    MPI_Alltoallv(send_buf.data(), to_send_counts.data(), sdispls.data(), MPI_UNSIGNED,
                  recv_buf.data(), to_recv_counts.data(), rdispls.data(), MPI_UNSIGNED,
                  MPI_COMM_WORLD);

    // Step 5: Parallel local sort/placement of received data (OpenMP)
    local_data = recv_buf;
    int n_received = local_data.size();
    if (n_received > 0) {
        std::vector<unsigned int> temp_buffer(n_received);
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

void radix_sort(std::vector<unsigned int>& data, int rank, int world_size) {
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
        sendcounts[i] = chunk_size + (i < remainder ? 1 : 0);
        if (i > 0) displs[i] = displs[i - 1] + sendcounts[i - 1];
    }

    std::vector<unsigned int> local_data(sendcounts[rank]);
    // Ensure send buffer is valid on rank 0
    if (rank == 0 && data.empty()) {
        data.resize(n_global);
    }
    MPI_Scatterv(rank == 0 ? data.data() : nullptr, sendcounts.data(), displs.data(), MPI_UNSIGNED,
                 local_data.data(), sendcounts[rank], MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    for (int i = 0; i < 4; ++i) {
        radix_sort_pass(local_data, i, rank, world_size);
    }

    // Gather all sorted parts to the root process
    int local_size = local_data.size();
    std::vector<int> recvcounts(world_size);
    MPI_Gather(&local_size, 1, MPI_INT, recvcounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        displs[0] = 0;
        for (int i = 1; i < world_size; i++) {
            displs[i] = displs[i - 1] + recvcounts[i - 1];
        }
        data.resize(n_global);
    }

    MPI_Gatherv(local_data.data(), local_data.size(), MPI_UNSIGNED,
                rank == 0 ? data.data() : nullptr, recvcounts.data(), displs.data(), MPI_UNSIGNED,
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