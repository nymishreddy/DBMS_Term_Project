#include "disk_manager.h"
#include "data_file.h"
#include "rtree.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <stdexcept>

std::vector<std::vector<float>> LoadBinary(const std::string& filename, int& out_dim) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("\n❌ FATAL: Could not open " + filename + ". Did the Python script run in this directory?");
    }

    int num_records = 0;
    file.read(reinterpret_cast<char*>(&num_records), sizeof(int));
    file.read(reinterpret_cast<char*>(&out_dim), sizeof(int));

    std::cout << "  -> Loaded file: " << num_records << " records, " << out_dim << " Dimensions.\n";

    // Sanity check to prevent out-of-memory loops
    if (num_records <= 0 || num_records > 1000000) {
        throw std::runtime_error("\n❌ FATAL: num_records seems corrupted: " + std::to_string(num_records));
    }

    std::vector<std::vector<float>> dataset(num_records, std::vector<float>(out_dim));
    for (int i = 0; i < num_records; ++i) {
        file.read(reinterpret_cast<char*>(dataset[i].data()), out_dim * sizeof(float));
    }
    return dataset;
}

void RunBenchmark(int dim) {
    std::string bin_file = "embeddings_" + std::to_string(dim) + "D.bin";
   
    std::cout << "\n========================================\n";
    std::cout << "  Benchmarking " << dim << "D Space\n";
    std::cout << "========================================\n";

    int loaded_dim;
    auto dataset = LoadBinary(bin_file, loaded_dim);
   
    std::string data_db = "bench_data_" + std::to_string(dim) + "D.db";
    std::string tree_db = "bench_tree_" + std::to_string(dim) + "D.db";
   
    // Nuke old files
    std::remove(data_db.c_str());
    std::remove(tree_db.c_str());

    DiskManager dm_data(data_db);
    DiskManager dm_tree(tree_db);
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::cout << "  -> Starting R-Tree Insertions...\n";
   
    auto start_build = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < dataset.size(); ++i) {
        rt.Insert(dataset[i]);
       
        // Print progress every 1000 items
        if ((i + 1) % 1000 == 0) {
            std::cout << "     ... Inserted " << (i + 1) << " / " << dataset.size() << " records.\n";
        }
    }
    auto end_build = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> build_time = end_build - start_build;
   
    std::cout << "  ✅ Build Complete in " << build_time.count() << " seconds\n";
    std::cout << "  -> Tree Height: " << rt.GetTreeHeight() << "\n\n";

    std::cout << "  -> Starting k-NN Queries...\n";
    int num_queries = 100;
    double total_query_time = 0.0;
    long long total_page_fetches = 0;

    for (int i = 0; i < num_queries; ++i) {
        // NOTE: Only call dm_tree.ResetReadCount() if you actually added it to disk_manager.h/cpp!
        // If you didn't add the read_count_ tracker yet, comment these two lines out to avoid compile errors.
        // dm_tree.ResetReadCount();
       
        auto start_query = std::chrono::high_resolution_clock::now();
        auto results = rt.KNNSearch(dataset[i], 10);
        auto end_query = std::chrono::high_resolution_clock::now();
       
        total_query_time += std::chrono::duration<double>(end_query - start_query).count();
        // total_page_fetches += dm_tree.GetReadCount();
    }

    std::cout << "  ✅ Queries Complete!\n";
    std::cout << "  -> Avg Query Time: " << (total_query_time / num_queries) * 1000.0 << " ms\n";
}

int main() {
    std::vector<int> dims = {10, 20, 30, 50};
    for (int dim : dims) {
        RunBenchmark(dim);
    }
    return 0;
}