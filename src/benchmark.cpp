#include "disk_manager.h"
#include "data_file.h"
#include "rtree.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

std::vector<std::vector<float>> LoadBinary(const std::string& filename, int& out_dim) {
    std::ifstream file(filename, std::ios::binary);
    int num_records = 0;
    file.read(reinterpret_cast<char*>(&num_records), sizeof(int));
    file.read(reinterpret_cast<char*>(&out_dim), sizeof(int));

    std::vector<std::vector<float>> dataset(num_records, std::vector<float>(out_dim));
    for (int i = 0; i < num_records; ++i) {
        file.read(reinterpret_cast<char*>(dataset[i].data()), out_dim * sizeof(float));
    }
    return dataset;
}

void RunBenchmark(int dim) {
    std::string bin_file = "embeddings_" + std::to_string(dim) + "D.bin";
    int loaded_dim;
    auto dataset = LoadBinary(bin_file, loaded_dim);
    
    std::string data_db = "bench_data_" + std::to_string(dim) + "D.db";
    std::string tree_db = "bench_tree_" + std::to_string(dim) + "D.db";
    std::remove(data_db.c_str());
    std::remove(tree_db.c_str());

    DiskManager dm_data(data_db);
    DiskManager dm_tree(tree_db);
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::cout << "\n========================================\n";
    std::cout << "  Benchmarking " << dim << "D Space\n";
    std::cout << "========================================\n";

    // 1. Measure Build Time
    auto start_build = std::chrono::high_resolution_clock::now();
    for (const auto& vec : dataset) {
        rt.Insert(vec);
    }
    auto end_build = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> build_time = end_build - start_build;
    
    std::cout << "Build Time:   " << build_time.count() << " seconds\n";
    std::cout << "Tree Height:  " << rt.GetTreeHeight() << "\n";

    // 2. Measure Query Performance
    int num_queries = 100;
    double total_query_time = 0.0;
    long long total_page_fetches = 0;

    for (int i = 0; i < num_queries; ++i) {
        dm_tree.ResetReadCount(); // Assuming you added this!
        
        auto start_query = std::chrono::high_resolution_clock::now();
        auto results = rt.KNNSearch(dataset[i], 10); // 10-NN search
        auto end_query = std::chrono::high_resolution_clock::now();
        
        total_query_time += std::chrono::duration<double>(end_query - start_query).count();
        total_page_fetches += dm_tree.GetReadCount();
    }

    std::cout << "Avg Query Time: " << (total_query_time / num_queries) * 1000.0 << " ms\n";
    std::cout << "Avg Pages Read: " << (total_page_fetches / num_queries) << " pages/query\n";
}

int main() {
    std::vector<int> dims = {10, 20, 30, 50};
    for (int dim : dims) {
        RunBenchmark(dim);
    }
    return 0;
}