#ifndef RTREE_H
#define RTREE_H

#include "rtree_file.h"
#include "data_file.h"
#include "buffer_pool_manager.h"
#include <vector>
#include <utility>

class RTree
{
public:
    RTree(DiskManager* tree_dm, BufferPoolManager* tree_bpm,
          DataFile* data_file, int dim);

    int Insert(const std::vector<float>& vec);
    std::vector<int> RangeSearch(const MBR& query_mbr) const;
    std::vector<std::pair<int, float>> KNNSearch(
        const std::vector<float>& query_pt, int k) const;

    int GetDim()        const { return dim_; }
    int GetTreeHeight() const;

private:
    RTreeFile rtf_;
    DataFile* data_file_;
    int dim_;
    int M_;
    int m_;

    int  ChooseLeaf(const MBR& mbr) const;
    int  ChooseLeafRecursive(int node_pid, const MBR& mbr,
                             std::vector<int>& path) const;
    int  QuadraticSplit(RTreeNode& node, const RTreeEntry& new_entry);
    std::pair<int,int> PickSeeds(const std::vector<RTreeEntry>& entries) const;
    int  PickNext(const std::vector<RTreeEntry>& remaining,
                  const MBR& mbr1, const MBR& mbr2) const;
    int  AdjustTree(std::vector<int>& path, int left_pid, int right_pid);

    void RangeSearchRecursive(int node_pid, const MBR& query_mbr,
                              std::vector<int>& results) const;
};

#endif // RTREE_H
