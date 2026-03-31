#ifndef RTREE_H
#define RTREE_H

#include "rtree_file.h"
#include "data_file.h"
#include <vector>
#include <utility> // pair

// ─────────────────────────────────────────────────────────────────────────────
// RTree — the public interface
//
// Usage:
//   DiskManager dm_data("vectors.db");
//   DiskManager dm_tree("tree.db");
//   DataFile    df(&dm_data, dim);
//   RTree       rt(&dm_tree, &df, dim);
//
//   int rid = rt.Insert(vec);            // returns record_id
//   auto hits = rt.RangeSearch(query_mbr);
//   auto nn   = rt.KNNSearch(query_pt, k);
// ─────────────────────────────────────────────────────────────────────────────
class RTree
{
public:
    RTree(DiskManager *tree_dm, DataFile *data_file, int dim);

    // Insert a vector; stores it in DataFile, indexes in tree
    // Returns the record_id assigned by DataFile
    int Insert(const std::vector<float> &vec);

    // Range search: return all record_ids whose stored vector is inside query_mbr
    std::vector<int> RangeSearch(const MBR &query_mbr) const;

    // kNN: return k nearest record_ids paired with their squared distances
    // Results sorted closest-first
    std::vector<std::pair<int, float>> KNNSearch(
        const std::vector<float> &query_pt, int k) const;

    int GetDim() const { return dim_; }
    int GetTreeHeight() const; // for diagnostics

private:
    RTreeFile rtf_;
    DataFile *data_file_;
    int dim_;
    int M_; // max entries per node
    int m_; // min entries per node

    // ── Insert internals ─────────────────────────────────────────────────────

    // Traverse to the best leaf, returning its page_id
    int ChooseLeaf(const MBR &mbr) const;

    // Recursive helper; path stores page_ids from root to leaf
    int ChooseLeafRecursive(int node_pid,
                            const MBR &mbr,
                            std::vector<int> &path) const;

    // Split a full node using Quadratic Split (Guttman 1984)
    // Returns the page_id of the new sibling node
    // Modifies 'node' in place (it keeps roughly half the entries)
    int QuadraticSplit(RTreeNode &node, const RTreeEntry &new_entry);

    // Pick the two seed entries for quadratic split
    std::pair<int, int> PickSeeds(const std::vector<RTreeEntry> &entries) const;

    // Pick the next entry to assign during split
    int PickNext(const std::vector<RTreeEntry> &remaining,
                 const MBR &mbr1, const MBR &mbr2) const;

    // Walk back up the path, adjusting MBRs and propagating splits
    // Returns new root page_id if root was split, else -1
    int AdjustTree(std::vector<int> &path,
                   int left_pid, int right_pid /* -1 if no split */);

    // ── Search internals ─────────────────────────────────────────────────────
    void RangeSearchRecursive(int node_pid,
                              const MBR &query_mbr,
                              std::vector<int> &results) const;
};

#endif // RTREE_H