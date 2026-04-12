#include "disk_manager.h"
#include "data_file.h"
#include "rtree.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <cmath>

// Helper to calculate squared Euclidean distance
float CalcDistSq(const std::vector<float>& a, const std::vector<float>& b) {
    float dist = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        dist += diff * diff;
    }
    return dist;
}

// Binary Loader
std::vector<std::vector<float>> LoadBinary(const std::string& filename, int& out_dim) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "  ⚠️ Could not open " << filename << ". Skipping.\n";
        return {};
    }

    int num_records = 0;
    file.read(reinterpret_cast<char*>(&num_records), sizeof(int));
    file.read(reinterpret_cast<char*>(&out_dim), sizeof(int));

    std::vector<std::vector<float>> dataset(num_records, std::vector<float>(out_dim));
    for (int i = 0; i < num_records; ++i) {
        file.read(reinterpret_cast<char*>(dataset[i].data()), out_dim * sizeof(float));
    }
    return dataset;
}

void VerifyQueriesForDimension(int dim) {
    std::string bin_file = "embeddings_" + std::to_string(dim) + "D.bin";
    std::string data_db = "bench_data_" + std::to_string(dim) + "D.db";
    std::string tree_db = "bench_tree_" + std::to_string(dim) + "D.db";

    std::cout << "\n========================================\n";
    std::cout << "  Verifying " << dim << "D Existing Databases\n";
    std::cout << "========================================\n";

    // 1. Load the in-memory baseline data
    int loaded_dim;
    auto dataset = LoadBinary(bin_file, loaded_dim);
    if (dataset.empty()) return;

    // 2. Connect to the existing R-Tree databases
    // Notice we do NOT use std::remove() here! We want the existing data.
    DiskManager dm_data(data_db);
    DiskManager dm_tree(tree_db);
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::cout << "  -> Connected to DB. Tree Height: " << rt.GetTreeHeight()
              << ", Total Records: " << df.GetTotalRecords() << "\n";

    if (df.GetTotalRecords() == 0) {
        std::cout << "  ⚠️ Database is empty. Did the build finish for this dimension?\n";
        return;
    }

    // Set up a random query point based on an existing vector, but add noise
    // so we aren't just searching for an exact 0-distance match.
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> idx_dist(0, dataset.size() - 1);
    std::uniform_real_distribution<float> noise_dist(-2.0f, 2.0f);

    int query_idx = idx_dist(gen);
    std::vector<float> query_pt = dataset[query_idx];
    for (int d = 0; d < dim; ++d) {
        query_pt[d] += noise_dist(gen);
    }

    // ---------------------------------------------------------
    // TEST A: k-NN Search Verification
    // ---------------------------------------------------------
    std::cout << "  [Test A] Running 10-NN Search...\n";
    int k = 10;
    auto knn_results = rt.KNNSearch(query_pt, k);

    // Calculate baseline
    std::vector<std::pair<int, float>> baseline_knn;
    for (size_t i = 0; i < dataset.size(); ++i) {
        baseline_knn.push_back({static_cast<int>(i), CalcDistSq(query_pt, dataset[i])});
    }
    std::sort(baseline_knn.begin(), baseline_knn.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    bool knn_pass = true;
    for (int i = 0; i < k; ++i) {
        // Check if the distances match. We check distance rather than exact ID
        // because multiple points might have the exact same distance (ties).
        if (std::abs(knn_results[i].second - baseline_knn[i].second) > 1e-4) {
            knn_pass = false;
            break;
        }
    }

    if (knn_pass) {
        std::cout << "  ✅ k-NN Search matches Linear Scan exactly.\n";
    } else {
        std::cout << "  ❌ k-NN Search FAILED.\n";
        assert(false);
    }

    // ---------------------------------------------------------
    // TEST B: Range Search Verification
    // ---------------------------------------------------------
    std::cout << "  [Test B] Running Range Search...\n";
    MBR query_box(dim);
    std::vector<float>exact_pt=dataset[query_idx];
    for (int d = 0; d < dim; ++d) {
        // Create a bounding box around our random query point
        float v1 = exact_pt[d] - 0.8f;
        float v2 = exact_pt[d] + 0.8f;
        query_box.min_vals[d] = std::min(v1, v2);
        query_box.max_vals[d] = std::max(v1, v2);
    }

    auto range_results = rt.RangeSearch(query_box);
    std::sort(range_results.begin(), range_results.end()); // Sort IDs for direct comparison

    // Calculate baseline
    std::vector<int> baseline_range;
    for (size_t i = 0; i < dataset.size(); ++i) {
        if (query_box.ContainsPoint(dataset[i])) {
            baseline_range.push_back(static_cast<int>(i));
        }
    }
    std::sort(baseline_range.begin(), baseline_range.end());

    if (range_results.size() == baseline_range.size()) {
        bool range_pass = true;
        for (size_t i = 0; i < range_results.size(); ++i) {
            if (range_results[i] != baseline_range[i]) {
                range_pass = false;
                break;
            }
        }
        if (range_pass) {
            std::cout << "  ✅ Range Search matches Linear Scan perfectly (Found " << range_results.size() << " points).\n";
        } else {
            std::cout << "  ❌ Range Search FAILED (ID mismatch).\n";
            assert(false);
        }
    } else {
        std::cout << "  ❌ Range Search FAILED (Count mismatch. Tree: "
                  << range_results.size() << ", Baseline: " << baseline_range.size() << ").\n";
        assert(false);
    }
}

int main() {
    // Only testing the dimensions you successfully built before stopping
    std::vector<int> dims_to_test = {10, 20, 30};
   
    for (int dim : dims_to_test) {
        VerifyQueriesForDimension(dim);
    }
   
    std::cout << "\n🏆 All database queries verified successfully!\n";
    return 0;
}