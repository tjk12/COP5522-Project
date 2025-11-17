#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include <mpi.h>
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

// --- Basic MPI Merge Sort ---
void hybrid_sort_merge(std::vector<unsigned int>& data, int rank, int world_size) {
    // Step 1: Scatter data from root to all processes
    int n_global = data.size();
    std::vector<int> sendcounts(world_size);
    std::vector<int> displs(world_size, 0);
    int chunk_size = n_global / world_size;
    int remainder = n_global % world_size;

    for (int i = 0; i < world_size; ++i) {
        sendcounts[i] = chunk_size + (i < remainder ? 1 : 0);
        if (i > 0) {
            displs[i] = displs[i - 1] + sendcounts[i - 1];
        }
    }

    std::vector<unsigned int> local_data(sendcounts[rank]);
    MPI_Scatterv(data.data(), sendcounts.data(), displs.data(), MPI_UNSIGNED,
                 local_data.data(), sendcounts[rank], MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    // Step 2: Each process sorts its local data
    std::sort(local_data.begin(), local_data.end());

    // Step 3: Iterative tree-based merge
    for (int step = 1; step < world_size; step *= 2) {
        if (rank % (2 * step) != 0) {
            // This process is a sender
            int dest = rank - step;
            int local_size = local_data.size();
            MPI_Send(&local_size, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
            MPI_Send(local_data.data(), local_size, MPI_UNSIGNED, dest, 1, MPI_COMM_WORLD);
            break; // This process is done
        } else if (rank + step < world_size) {
            // This process is a receiver
            int src = rank + step;
            int received_size;
            MPI_Recv(&received_size, 1, MPI_INT, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            std::vector<unsigned int> received_data(received_size);
            MPI_Recv(received_data.data(), received_size, MPI_UNSIGNED, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            std::vector<unsigned int> merged_data(local_data.size() + received_size);
            std::merge(local_data.begin(), local_data.end(),
                       received_data.begin(), received_data.end(),
                       merged_data.begin());
            local_data = merged_data;
        }
    }

    // Step 4: Root (rank 0) now holds the final sorted data
    if (rank == 0) {
        data = local_data;
    }
}

// --- Basic MPI Radix Sort ---
void scalable_radix_sort(std::vector<unsigned int>& data, int rank, int world_size) {
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
    MPI_Scatterv(data.data(), sendcounts.data(), displs.data(), MPI_UNSIGNED,
                 local_data.data(), sendcounts[rank], MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    const int BUCKET_SIZE = 256;

    for (int i = 0; i < 4; ++i) { // 4 passes for 32-bit integers
        int shift = i * 8;

        // 1. Local histogram
        std::vector<int> local_hist(BUCKET_SIZE, 0);
        for (unsigned int val : local_data) {
            local_hist[(val >> shift) & 0xFF]++;
        }

        // 2. Global histogram via reduction
        std::vector<int> global_hist(BUCKET_SIZE, 0);
        MPI_Allreduce(local_hist.data(), global_hist.data(), BUCKET_SIZE, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

        // 3. Determine what to send to each process
        std::vector<int> to_send_counts(world_size, 0);
        std::vector<std::vector<unsigned int>> to_send_buckets(world_size);
        
        int buckets_per_proc = (BUCKET_SIZE + world_size - 1) / world_size;
        for (unsigned int val : local_data) {
            int bucket_idx = (val >> shift) & 0xFF;
            int dest_proc = std::min(bucket_idx / buckets_per_proc, world_size - 1);
            to_send_buckets[dest_proc].push_back(val);
        }
        for(int p=0; p<world_size; ++p) {
            to_send_counts[p] = to_send_buckets[p].size();
        }

        // 4. Inform each process how much data it will receive
        std::vector<int> to_recv_counts(world_size, 0);
        MPI_Alltoall(to_send_counts.data(), 1, MPI_INT, to_recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);

        // 5. Prepare send/recv buffers for Alltoallv
        std::vector<int> sdispls(world_size, 0), rdispls(world_size, 0);
        std::vector<unsigned int> send_buf;
        for(int p=0; p<world_size; ++p) {
            if (p > 0) sdispls[p] = sdispls[p-1] + to_send_counts[p-1];
            send_buf.insert(send_buf.end(), to_send_buckets[p].begin(), to_send_buckets[p].end());
        }
        int total_recv = 0;
        for(int p=0; p<world_size; ++p) {
            if (p > 0) rdispls[p] = rdispls[p-1] + to_recv_counts[p-1];
            total_recv += to_recv_counts[p];
        }
        std::vector<unsigned int> recv_buf(total_recv);

        // 6. Exchange data
        MPI_Alltoallv(send_buf.data(), to_send_counts.data(), sdispls.data(), MPI_UNSIGNED,
                      recv_buf.data(), to_recv_counts.data(), rdispls.data(), MPI_UNSIGNED,
                      MPI_COMM_WORLD);

        // 7. Locally sort the received data (it's from a limited range of buckets)
        std::sort(recv_buf.begin(), recv_buf.end());
        local_data = recv_buf;
    }

    // 8. Gather all sorted parts to the root process
    int local_size = local_data.size();
    MPI_Gather(&local_size, 1, MPI_INT, sendcounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        displs[0] = 0;
        for (int i = 1; i < world_size; i++) {
            displs[i] = displs[i-1] + sendcounts[i-1];
        }
        data.resize(n_global);
    }

    MPI_Gatherv(local_data.data(), local_data.size(), MPI_UNSIGNED,
                data.data(), sendcounts.data(), displs.data(), MPI_UNSIGNED,
                0, MPI_COMM_WORLD);
}


// --- Main Driver ---
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 3) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <algorithm> <filename>" << std::endl;
            std::cerr << "Note: Number of processes is determined by mpirun." << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    std::string algorithm = argv[1];
    std::string filename = argv[2];
    
    auto data = read_data(filename, rank);
    size_t N = data.size(); // N is only correct on rank 0 initially

    MPI_Barrier(MPI_COMM_WORLD);
    auto start = std::chrono::high_resolution_clock::now();
    
    if (algorithm == "hybrid_merge_sort") {
        hybrid_sort_merge(data, rank, world_size);
    } else if (algorithm == "scalable_radix_sort") {
        scalable_radix_sort(data, rank, world_size);
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
        
        // N was already read by rank 0, so it's correct here.
        double duration_s = duration_ms.count() / 1000.0;
        double mkeys_per_second = (duration_s > 0) ? (N / 1e6 / duration_s) : 0;

        json result = {
            {"algorithm", algorithm},
            {"N", N},
            {"threads", world_size}, // Using "threads" to match schema, but it's processes
            {"time_ms", duration_ms.count()},
            {"mkeys_per_s", mkeys_per_second},
            {"correct", sorted_correctly}
        };
        std::cout << result.dump() << std::endl;
    }
    
    MPI_Finalize();
    return 0;
}