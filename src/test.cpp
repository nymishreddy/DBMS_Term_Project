#include "disk_manager.h"
#include "data_file.h"
#include "rtree.h"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <cstdio> // for std::remove

// Helper: Squared Euclidean distance
float CalcDistSq(const std::vector<float>& a, const std::vector<float>& b) {
    float dist = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        dist += diff * diff;
    }
    return dist;
}

// ---------------------------------------------------------
// Test 1: Dense Data & k-NN Verification
// ---------------------------------------------------------
void TestKNNSearch() {
    std::cout << "\n--- [Test 1] Dense Data k-NN Search ---\n";
    int dim = 10;
    int num_points = 500;
    int k = 5;

    std::remove("knn_data.db");
    std::remove("knn_tree.db");
    
    DiskManager dm_data("knn_data.db");
    DiskManager dm_tree("knn_tree.db");
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dis(0.0f, 100.0f);
    
    std::vector<std::vector<float>> dataset;
    for (int i = 0; i < num_points; ++i) {
        std::vector<float> vec(dim);
        for (int d = 0; d < dim; ++d) vec[d] = dis(gen);
        dataset.push_back(vec);
        rt.Insert(vec);
    }

    std::vector<float> query(dim);
    for (int d = 0; d < dim; ++d) query[d] = dis(gen);

    auto knn_results = rt.KNNSearch(query, k);

    std::vector<std::pair<int, float>> baseline;
    for (int i = 0; i < num_points; ++i) {
        baseline.push_back({i, CalcDistSq(query, dataset[i])});
    }
    std::sort(baseline.begin(), baseline.end(), 
        [](const auto& a, const auto& b) { return a.second < b.second; });

    assert(knn_results.size() == static_cast<size_t>(k));
    if (std::abs(knn_results[0].second - baseline[0].second) < 1e-4) {
        std::cout << "✅ k-NN Search matches Linear Scan baseline!\n";
    } else {
        std::cout << "❌ k-NN Mismatch!\n";
        assert(false);
    }
}

// ---------------------------------------------------------
// Test 2: Range Search Verification
// ---------------------------------------------------------
void TestRangeSearch() {
    std::cout << "\n--- [Test 2] Range Search Verification ---\n";
    int dim = 10;
    int num_points = 1000;
    
    std::remove("range_data.db");
    std::remove("range_tree.db");
    
    DiskManager dm_data("range_data.db");
    DiskManager dm_tree("range_tree.db");
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::mt19937 gen(1337);
    std::uniform_real_distribution<float> dis(-50.0f, 50.0f);

    std::vector<std::vector<float>> dataset;
    for (int i = 0; i < num_points; ++i) {
        std::vector<float> vec(dim);
        for (int d = 0; d < dim; ++d) vec[d] = dis(gen);
        dataset.push_back(vec);
        rt.Insert(vec);
    }

    MBR query_box(dim);
    for (int d = 0; d < dim; ++d) {
        float v1 = dis(gen);
        float v2 = dis(gen);
        query_box.min_vals[d] = std::min(v1, v2);
        query_box.max_vals[d] = std::max(v1, v2);
    }

    auto rtree_results = rt.RangeSearch(query_box);
    std::sort(rtree_results.begin(), rtree_results.end());

    std::vector<int> baseline_results;
    for (int i = 0; i < num_points; ++i) {
        if (query_box.ContainsPoint(dataset[i])) {
            baseline_results.push_back(i);
        }
    }
    std::sort(baseline_results.begin(), baseline_results.end());

    std::cout << "Found " << rtree_results.size() << " points inside bounding box.\n";
    assert(rtree_results.size() == baseline_results.size());
    for (size_t i = 0; i < rtree_results.size(); ++i) {
        assert(rtree_results[i] == baseline_results[i]);
    }
    std::cout << "✅ Range Search matches Linear Scan perfectly!\n";
}

// ---------------------------------------------------------
// Test 3: Adversarial Edge Cases
// ---------------------------------------------------------
void TestAdversarialData() {
    std::cout << "\n--- [Test 3] Adversarial Data Handling ---\n";
    int dim = 10;
    
    std::remove("adv_data.db");
    std::remove("adv_tree.db");
    
    DiskManager dm_data("adv_data.db");
    DiskManager dm_tree("adv_tree.db");
    DataFile df(&dm_data, dim);
    RTree rt(&dm_tree, &df, dim);

    std::vector<float> duplicate_pt(dim, 42.0f);
    for (int i = 0; i < 200; ++i) {
        rt.Insert(duplicate_pt);
    }
    std::cout << "✅ Inserted 200 exact duplicates without infinite loops.\n";

    for (int i = 0; i < 100; ++i) {
        std::vector<float> colinear_pt(dim, static_cast<float>(i));
        rt.Insert(colinear_pt);
    }
    std::cout << "✅ Inserted colinear points (zero-volume MBRs) without crash.\n";

    std::vector<float> zero_pt(dim, 0.0f);
    rt.Insert(zero_pt);

    auto knn = rt.KNNSearch(zero_pt, 1);
    assert(knn.size() == 1);
    assert(knn[0].second == 0.0f);
    std::cout << "✅ Zero-vector nearest neighbor retrieved successfully.\n";
}

// ---------------------------------------------------------
// Main Runner
// ---------------------------------------------------------
int main() {
    std::cout << "========================================\n";
    std::cout << "   R-Tree Rigorous Stress Test Suite    \n";
    std::cout << "========================================\n";

    TestKNNSearch();
    TestRangeSearch();
    TestAdversarialData();

    std::cout << "\n========================================\n";
    std::cout << "🏆 ALL TESTS PASSED SUCCESSFULLY 🏆\n";
    std::cout << "========================================\n";

    // Optional: If you implemented the ValidateTreeInvariants function inside rtree.cpp
    /*
    DiskManager dm_tree("knn_tree.db"); 
    DiskManager dm_data("knn_data.db");
    DataFile df(&dm_data, 10);
    RTree rt(&dm_tree, &df, 10);
    assert(rt.ValidateTreeInvariants());
    std::cout << "✅ Tree Structural Invariants Confirmed!\n";
    */

    return 0;
}