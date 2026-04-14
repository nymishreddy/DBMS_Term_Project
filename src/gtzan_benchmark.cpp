#include "disk_manager.h"
#include "buffer_pool_manager.h"
#include "data_file.h"
#include "rtree.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <numeric>

// ── Loaders ──────────────────────────────────────────────────────────────────

// Raw float binary, no header: N * dim floats
std::vector<std::vector<float>> LoadEmbeddings(const std::string& path, int dim) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    f.seekg(0, std::ios::end);
    size_t bytes = f.tellg();
    f.seekg(0, std::ios::beg);

    int n = static_cast<int>(bytes / (dim * sizeof(float)));
    std::vector<std::vector<float>> data(n, std::vector<float>(dim));
    for (int i = 0; i < n; ++i)
        f.read(reinterpret_cast<char*>(data[i].data()), dim * sizeof(float));
    return data;
}

// Raw int32 binary, no header
std::vector<int> LoadLabels(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    f.seekg(0, std::ios::end);
    int n = static_cast<int>(f.tellg() / sizeof(int));
    f.seekg(0, std::ios::beg);

    std::vector<int> labels(n);
    f.read(reinterpret_cast<char*>(labels.data()), n * sizeof(int));
    return labels;
}

// ── Linear Scan KNN ──────────────────────────────────────────────────────────

float SquaredDist(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0;
    for (int i = 0; i < (int)a.size(); ++i) d += (a[i]-b[i])*(a[i]-b[i]);
    return d;
}

// Returns k nearest {record_id, dist_sq} sorted closest-first
std::vector<std::pair<int,float>> LinearKNN(
    const std::vector<std::vector<float>>& dataset,
    const std::vector<float>& query, int k)
{
    std::vector<std::pair<float,int>> dists;
    dists.reserve(dataset.size());
    for (int i = 0; i < (int)dataset.size(); ++i)
        dists.push_back({SquaredDist(dataset[i], query), i});
    std::partial_sort(dists.begin(), dists.begin() + k, dists.end());

    std::vector<std::pair<int,float>> result(k);
    for (int i = 0; i < k; ++i)
        result[i] = {dists[i].second, dists[i].first};
    return result;
}

// ── Accuracy: majority-vote label from KNN result ────────────────────────────

int MajorityLabel(const std::vector<std::pair<int,float>>& knn_result,
                  const std::vector<int>& labels)
{
    std::unordered_map<int,int> votes;
    for (auto& [rid, dist] : knn_result)
        votes[labels[rid]]++;
    return std::max_element(votes.begin(), votes.end(),
        [](auto& a, auto& b){ return a.second < b.second; })->first;
}

// ── Recall@k: fraction of linear-scan neighbors found by rtree ───────────────

float RecallAtK(const std::vector<std::pair<int,float>>& rtree_result,
                const std::vector<std::pair<int,float>>& linear_result)
{
    int found = 0;
    for (auto& [rid, d] : rtree_result)
        for (auto& [rid2, d2] : linear_result)
            if (rid == rid2) { found++; break; }
    return static_cast<float>(found) / static_cast<float>(linear_result.size());
}

// ── Per-run benchmark ─────────────────────────────────────────────────────────

void RunBenchmark(const std::string& emb_path, int dim,
                  const std::vector<int>& labels,
                  const std::string& tag)
{
    auto dataset = LoadEmbeddings(emb_path, dim);
    int  N       = static_cast<int>(dataset.size());
    int  K       = 5;
    int  n_query = 100; // use first 100 as queries

    std::cout << "\n========================================\n";
    std::cout << "  " << tag << "  |  N=" << N << "  dim=" << dim << "\n";
    std::cout << "========================================\n";

    // ── Build R-Tree ─────────────────────────────────────────────────────────
    std::string data_db = "gtzan_data_" + tag + ".db";
    std::string tree_db = "gtzan_tree_" + tag + ".db";
    std::remove(data_db.c_str());
    std::remove(tree_db.c_str());

    DiskManager        dm_data(data_db), dm_tree(tree_db);
    BufferPoolManager  bpm_data(&dm_data, 512), bpm_tree(&dm_tree, 512);
    DataFile           df(&dm_data, &bpm_data, dim);
    RTree              rt(&dm_tree, &bpm_tree, &df, dim);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (const auto& v : dataset) rt.Insert(v);
    auto t1 = std::chrono::high_resolution_clock::now();
    double build_ms = std::chrono::duration<double,std::milli>(t1-t0).count();

    std::cout << "Build Time  : " << build_ms      << " ms\n";
    std::cout << "Tree Height : " << rt.GetTreeHeight() << "\n";

    // ── Query loop ────────────────────────────────────────────────────────────
    double rtree_total_ms  = 0, linear_total_ms = 0;
    long long rtree_pages  = 0;
    double total_recall    = 0;
    int rtree_correct = 0, linear_correct = 0;

    for (int i = 0; i < n_query; ++i) {
        const auto& q = dataset[i];

        // R-Tree KNN
        dm_tree.ResetReadCount();
        auto qt0 = std::chrono::high_resolution_clock::now();
        auto rt_res = rt.KNNSearch(q, K);
        auto qt1 = std::chrono::high_resolution_clock::now();
        rtree_total_ms += std::chrono::duration<double,std::milli>(qt1-qt0).count();
        rtree_pages    += dm_tree.GetReadCount();

        // Linear scan KNN
        auto lt0 = std::chrono::high_resolution_clock::now();
        auto lin_res = LinearKNN(dataset, q, K);
        auto lt1 = std::chrono::high_resolution_clock::now();
        linear_total_ms += std::chrono::duration<double,std::milli>(lt1-lt0).count();

        // Recall (how many of linear's neighbors did rtree find?)
        total_recall += RecallAtK(rt_res, lin_res);

        // Classification accuracy (majority vote, skip self at index 0)
        // query itself is in the dataset at index i, so ground truth = labels[i]
        int true_label = labels[i];
        if (MajorityLabel(rt_res,  labels) == true_label) rtree_correct++;
        if (MajorityLabel(lin_res, labels) == true_label) linear_correct++;
    }

    double avg_recall   = total_recall / n_query * 100.0;
    double rtree_acc    = rtree_correct  * 100.0 / n_query;
    double linear_acc   = linear_correct * 100.0 / n_query;

    std::cout << "\n--- R-Tree KNN (k=" << K << ") ---\n";
    std::cout << "Avg Query Time : " << rtree_total_ms  / n_query << " ms\n";
    std::cout << "Avg Pages Read : " << rtree_pages     / n_query << " pages/query\n";
    std::cout << "Recall@"  << K    << "          : " << avg_recall    << " %\n";
    std::cout << "Classification : " << rtree_acc        << " % accuracy\n";

    std::cout << "\n--- Linear Scan KNN (k=" << K << ") ---\n";
    std::cout << "Avg Query Time : " << linear_total_ms / n_query << " ms\n";
    std::cout << "Classification : " << linear_acc       << " % accuracy\n";

    std::cout << "\n--- Speedup ---\n";
    std::cout << "R-Tree is " << linear_total_ms / rtree_total_ms
              << "x faster than linear scan\n";
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    auto labels = LoadLabels("labels.bin");
    RunBenchmark("gtzan_pca_2.bin", 2, labels, "2D");
    RunBenchmark("gtzan_pca_3.bin", 3, labels, "3D");
    RunBenchmark("gtzan_pca_6.bin",  6,  labels, "6D");
    RunBenchmark("gtzan_pca_10.bin", 10, labels, "10D");
    
    // main() now calls:
RunBenchmark("gtzan_pca_15.bin", 15, labels, "15D");
RunBenchmark("gtzan_pca_20.bin", 20, labels, "20D");
RunBenchmark("gtzan_pca_30.bin", 30, labels, "30D");


    return 0;
}