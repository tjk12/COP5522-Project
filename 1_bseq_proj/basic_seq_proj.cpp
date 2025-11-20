#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <numeric>
#include "json.hpp"

using json = nlohmann::json;

// Utility Functions
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

// --- Basic Sequential Merge Sort (Recursive) ---
void merge_sort_recursive(std::vector<unsigned int>& data, std::vector<unsigned int>& temp, int left, int right) {
    if (left >= right) {
        return;
    }
    int mid = left + (right - left) / 2;
    merge_sort_recursive(data, temp, left, mid);
    merge_sort_recursive(data, temp, mid + 1, right);
    std::merge(data.begin() + left, data.begin() + mid + 1,
               data.begin() + mid + 1, data.begin() + right + 1,
               temp.begin() + left);
    std::copy(temp.begin() + left, temp.begin() + right + 1, data.begin() + left);
}

void merge_sort(std::vector<unsigned int>& data) {
    if (data.empty()) return;
    std::vector<unsigned int> temp(data.size());
    merge_sort_recursive(data, temp, 0, data.size() - 1);
}

// --- Basic Sequential Radix Sort (using buckets) ---
void radix_sort_pass(std::vector<unsigned int>& data, int byte_num) {
    int n = data.size();
    if (n == 0) return;

    int shift = byte_num * 8;
    const int BUCKET_SIZE = 256;

    // Create 256 buckets
    std::vector<std::vector<unsigned int>> buckets(BUCKET_SIZE);

    // Step 1: Distribute elements into buckets
    for (int i = 0; i < n; ++i) {
        int bucket_index = (data[i] >> shift) & 0xFF;
        buckets[bucket_index].push_back(data[i]);
    }

    // Step 2: Gather elements from buckets back into the original array
    int current_pos = 0;
    for (int i = 0; i < BUCKET_SIZE; ++i) {
        for (unsigned int val : buckets[i]) {
            data[current_pos++] = val;
        }
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
        {"Title", "Basic Sequential Sorter"},
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