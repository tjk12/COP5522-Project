#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <omp.h>
#include "json.hpp" 

using json = nlohmann::json;

// --- Utility Functions ---

// FIX APPLIED: This function now correctly reads 100-byte records from gensort
// and extracts the first 4 bytes as the integer key.
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

// Checks if the vector is sorted
bool is_sorted(const std::vector<unsigned int>& data) {
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] > data[i + 1]) {
            return false;
        }
    }
    return true;
}

// --- Introsort Implementation ---
void heapsort(std::vector<unsigned int>::iterator begin, std::vector<unsigned int>::iterator end) {
    std::make_heap(begin, end);
    std::sort_heap(begin, end);
}

void insertion_sort(std::vector<unsigned int>::iterator begin, std::vector<unsigned int>::iterator end) {
    std::sort(begin, end);
}

template<typename Iterator>
void introsort_recursive(Iterator begin, Iterator end, int max_depth, int cutoff) {
    if (std::distance(begin, end) <= cutoff) {
        insertion_sort(begin, end);
        return;
    }
    if (max_depth == 0) {
        heapsort(begin, end);
        return;
    }
    Iterator pivot = std::partition(begin + 1, end, [begin](unsigned int val) { return val < *begin; });
    std::iter_swap(begin, pivot - 1);
    #pragma omp task
    { introsort_recursive(begin, pivot - 1, max_depth - 1, cutoff); }
    #pragma omp task
    { introsort_recursive(pivot, end, max_depth - 1, cutoff); }
}

void parallel_introsort(std::vector<unsigned int>& data, int cutoff) {
    if (data.empty()) return;
    int max_depth = 2 * log2(data.size());
    #pragma omp parallel
    {
        #pragma omp single
        {
            introsort_recursive(data.begin(), data.end(), max_depth, cutoff);
        }
    }
}

// --- Radix Sort Implementation ---
void counting_sort_by_byte(std::vector<unsigned int>& data, int byte_num) {
    int n = data.size();
    std::vector<unsigned int> output(n);
    std::vector<int> count(256, 0);
    int shift = byte_num * 8;
    #pragma omp parallel
    {
        std::vector<int> p_count(256, 0);
        #pragma omp for nowait
        for (int i = 0; i < n; i++) {
            p_count[(data[i] >> shift) & 0xFF]++;
        }
        #pragma omp critical
        for (int i = 0; i < 256; i++) {
            count[i] += p_count[i];
        }
    }
    for (int i = 1; i < 256; i++) {
        count[i] += count[i - 1];
    }
    for (int i = n - 1; i >= 0; i--) {
        output[count[(data[i] >> shift) & 0xFF] - 1] = data[i];
        count[(data[i] >> shift) & 0xFF]--;
    }
    #pragma omp parallel for
    for (int i = 0; i < n; i++) {
        data[i] = output[i];
    }
}

void parallel_radix_sort(std::vector<unsigned int>& data) {
    for (int i = 0; i < 4; ++i) { // For 32-bit integers
        counting_sort_by_byte(data, i);
    }
}

// --- Main Driver ---
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <algorithm> <threads> <filename> [cutoff]" << std::endl;
        return 1;
    }
    std::string algorithm = argv[1];
    int threads = std::stoi(argv[2]);
    std::string filename = argv[3];
    int cutoff = (algorithm == "introsort" && argc > 4) ? std::stoi(argv[4]) : 0;
    omp_set_num_threads(threads);
    auto data = read_data(filename);
    size_t N = data.size();
    auto start = std::chrono::high_resolution_clock::now();
    if (algorithm == "introsort") {
        parallel_introsort(data, cutoff);
    } else if (algorithm == "radix_sort") {
        parallel_radix_sort(data);
    } else {
        std::cerr << "Unknown algorithm: " << algorithm << std::endl;
        return 1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    bool sorted_correctly = is_sorted(data);
    json result = {
        {"algorithm", algorithm}, {"N", N}, {"threads", threads},
        {"cutoff", cutoff}, {"time_ms", duration.count()}, {"correct", sorted_correctly}
    };
    std::cout << result.dump() << std::endl;
    return 0;
}