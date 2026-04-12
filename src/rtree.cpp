#include "rtree.h"
#include <algorithm>
#include <queue>
#include <stdexcept>
#include <cassert>
#include <limits>

RTree::RTree(DiskManager* tree_dm, BufferPoolManager* tree_bpm,
             DataFile* data_file, int dim)
    : rtf_(tree_dm, tree_bpm, dim),
      data_file_(data_file),
      dim_(dim),
      M_(RTreeNode::MaxEntries(dim)),
      m_(RTreeNode::MinEntries(dim)) {}

int RTree::Insert(const std::vector<float>& vec)
{
    int record_id = data_file_->AppendRecord(vec);

    MBR point_mbr = MBRFromPoint(vec);

    RTreeEntry new_entry(dim_);
    new_entry.mbr = point_mbr;
    new_entry.child_or_record = record_id;

    std::vector<int> path;
    int leaf_pid = ChooseLeafRecursive(rtf_.GetRootPageId(), point_mbr, path);
    path.push_back(leaf_pid);

    RTreeNode leaf;
    rtf_.ReadNode(leaf_pid, leaf);

    int split_pid = -1;
    if (!leaf.IsFull(dim_)) {
        leaf.AddEntry(new_entry);
        rtf_.WriteNode(leaf);
    } else {
        split_pid = QuadraticSplit(leaf, new_entry);
    }

    path.pop_back();
    int new_root = AdjustTree(path, leaf_pid, split_pid);
    (void)new_root;

    return record_id;
}

int RTree::ChooseLeafRecursive(int node_pid, const MBR& mbr,
                               std::vector<int>& path) const
{
    RTreeNode node;
    rtf_.ReadNode(node_pid, node);

    if (node.is_leaf)
        return node_pid;

    path.push_back(node_pid);

    int best_idx = 0;
    float best_expansion = std::numeric_limits<float>::max();
    float best_volume    = std::numeric_limits<float>::max();

    for (int i = 0; i < node.count; ++i) {
        float expansion = node.entries[i].mbr.ExpansionNeeded(mbr);
        float volume    = node.entries[i].mbr.Volume();
        if (expansion < best_expansion ||
            (expansion == best_expansion && volume < best_volume))
        {
            best_expansion = expansion;
            best_volume    = volume;
            best_idx       = i;
        }
    }

    return ChooseLeafRecursive(node.entries[best_idx].child_or_record,
                               mbr, path);
}

int RTree::ChooseLeaf(const MBR& mbr) const
{
    std::vector<int> dummy;
    return ChooseLeafRecursive(rtf_.GetRootPageId(), mbr, dummy);
}

int RTree::QuadraticSplit(RTreeNode& node, const RTreeEntry& new_entry)
{
    std::vector<RTreeEntry> all = node.entries;
    all.push_back(new_entry);

    auto [s1, s2] = PickSeeds(all);

    std::vector<RTreeEntry> g1, g2;
    MBR mbr1(dim_), mbr2(dim_);

    g1.push_back(all[s1]); mbr1.ExpandToIncludeMBR(all[s1].mbr);
    g2.push_back(all[s2]); mbr2.ExpandToIncludeMBR(all[s2].mbr);

    std::vector<bool> assigned(all.size(), false);
    assigned[s1] = true;
    assigned[s2] = true;

    int remaining = static_cast<int>(all.size()) - 2;

    while (remaining > 0) {
        int g1_needs = m_ - static_cast<int>(g1.size());
        int g2_needs = m_ - static_cast<int>(g2.size());

        if (g1_needs >= remaining) {
            for (size_t i = 0; i < all.size(); ++i)
                if (!assigned[i]) {
                    g1.push_back(all[i]);
                    mbr1.ExpandToIncludeMBR(all[i].mbr);
                    assigned[i] = true;
                }
            break;
        }
        if (g2_needs >= remaining) {
            for (size_t i = 0; i < all.size(); ++i)
                if (!assigned[i]) {
                    g2.push_back(all[i]);
                    mbr2.ExpandToIncludeMBR(all[i].mbr);
                    assigned[i] = true;
                }
            break;
        }

        std::vector<RTreeEntry> unassigned;
        std::vector<int> unassigned_idx;
        for (size_t i = 0; i < all.size(); ++i)
            if (!assigned[i]) {
                unassigned.push_back(all[i]);
                unassigned_idx.push_back(static_cast<int>(i));
            }

        int next_idx = -1;
        float best_diff = -1.0f;
        for (size_t j = 0; j < unassigned.size(); ++j) {
            float d1   = mbr1.ExpansionNeeded(unassigned[j].mbr);
            float d2   = mbr2.ExpansionNeeded(unassigned[j].mbr);
            float diff = std::abs(d1 - d2);
            if (diff > best_diff) { best_diff = diff; next_idx = static_cast<int>(j); }
        }

        const RTreeEntry& chosen = unassigned[next_idx];
        float d1 = mbr1.ExpansionNeeded(chosen.mbr);
        float d2 = mbr2.ExpansionNeeded(chosen.mbr);

        if (d1 < d2 ||
            (d1 == d2 && mbr1.Volume() < mbr2.Volume()) ||
            (d1 == d2 && mbr1.Volume() == mbr2.Volume() && g1.size() <= g2.size()))
        {
            g1.push_back(chosen); mbr1.ExpandToIncludeMBR(chosen.mbr);
        } else {
            g2.push_back(chosen); mbr2.ExpandToIncludeMBR(chosen.mbr);
        }

        assigned[unassigned_idx[next_idx]] = true;
        --remaining;
    }

    node.entries = g1;
    node.count   = static_cast<int>(g1.size());
    rtf_.WriteNode(node);

    int sibling_pid = rtf_.AllocateNode();
    RTreeNode sibling;
    sibling.is_leaf  = node.is_leaf;
    sibling.count    = static_cast<int>(g2.size());
    sibling.entries  = g2;
    sibling.page_id  = sibling_pid;
    rtf_.WriteNode(sibling);

    return sibling_pid;
}

std::pair<int,int> RTree::PickSeeds(const std::vector<RTreeEntry>& entries) const
{
    int s1 = 0, s2 = 1;
    float worst_waste = -std::numeric_limits<float>::max();

    for (size_t i = 0; i < entries.size(); ++i)
        for (size_t j = i + 1; j < entries.size(); ++j) {
            MBR combined = MBRUnion(entries[i].mbr, entries[j].mbr);
            float waste  = combined.Volume()
                         - entries[i].mbr.Volume()
                         - entries[j].mbr.Volume();
            if (waste > worst_waste) {
                worst_waste = waste;
                s1 = static_cast<int>(i);
                s2 = static_cast<int>(j);
            }
        }
    return {s1, s2};
}

int RTree::AdjustTree(std::vector<int>& path, int left_pid, int right_pid)
{
    while (!path.empty()) {
        int parent_pid = path.back();
        path.pop_back();

        RTreeNode parent;
        rtf_.ReadNode(parent_pid, parent);

        RTreeNode left_child;
        rtf_.ReadNode(left_pid, left_child);
        MBR left_mbr = left_child.ComputeBoundingMBR();

        for (int i = 0; i < parent.count; ++i)
            if (parent.entries[i].child_or_record == left_pid) {
                parent.entries[i].mbr = left_mbr;
                break;
            }

        int new_split = -1;
        if (right_pid != -1) {
            RTreeNode right_child;
            rtf_.ReadNode(right_pid, right_child);
            MBR right_mbr = right_child.ComputeBoundingMBR();

            RTreeEntry sibling_entry(dim_);
            sibling_entry.mbr             = right_mbr;
            sibling_entry.child_or_record = right_pid;

            if (!parent.IsFull(dim_)) {
                parent.AddEntry(sibling_entry);
                rtf_.WriteNode(parent);
                right_pid = -1;
            } else {
                rtf_.WriteNode(parent);
                new_split = QuadraticSplit(parent, sibling_entry);
                right_pid = new_split;
            }
        } else {
            rtf_.WriteNode(parent);
        }

        left_pid = parent_pid;
    }

    if (right_pid != -1) {
        int new_root_pid = rtf_.AllocateNode();

        RTreeNode left_node, right_node;
        rtf_.ReadNode(left_pid,  left_node);
        rtf_.ReadNode(right_pid, right_node);

        RTreeEntry e1(dim_), e2(dim_);
        e1.mbr             = left_node.ComputeBoundingMBR();
        e1.child_or_record = left_pid;
        e2.mbr             = right_node.ComputeBoundingMBR();
        e2.child_or_record = right_pid;

        RTreeNode new_root;
        new_root.is_leaf  = false;
        new_root.count    = 0;
        new_root.page_id  = new_root_pid;
        new_root.AddEntry(e1);
        new_root.AddEntry(e2);
        rtf_.WriteNode(new_root);

        rtf_.SetRootPageId(new_root_pid);
        return new_root_pid;
    }

    return -1;
}

std::vector<int> RTree::RangeSearch(const MBR& query_mbr) const
{
    std::vector<int> results;
    RangeSearchRecursive(rtf_.GetRootPageId(), query_mbr, results);
    return results;
}

void RTree::RangeSearchRecursive(int node_pid, const MBR& query_mbr,
                                 std::vector<int>& results) const
{
    RTreeNode node;
    rtf_.ReadNode(node_pid, node);

    for (int i = 0; i < node.count; ++i) {
        if (!node.entries[i].mbr.Overlaps(query_mbr))
            continue;
        if (node.is_leaf)
            results.push_back(node.entries[i].child_or_record);
        else
            RangeSearchRecursive(node.entries[i].child_or_record,
                                 query_mbr, results);
    }
}

std::vector<std::pair<int, float>> RTree::KNNSearch(
    const std::vector<float>& query_pt, int k) const
{
    using QItem = std::tuple<float, int, bool>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;

    {
        RTreeNode root;
        rtf_.ReadNode(rtf_.GetRootPageId(), root);
        MBR root_mbr = root.ComputeBoundingMBR();
        pq.push({root_mbr.MinDistSq(query_pt), rtf_.GetRootPageId(), true});
    }

    std::vector<std::pair<int, float>> results;

    while (!pq.empty() && static_cast<int>(results.size()) < k) {
        auto [dist_sq, id, is_node] = pq.top();
        pq.pop();

        if (!is_node) {
            results.push_back({id, dist_sq});
            continue;
        }

        RTreeNode node;
        rtf_.ReadNode(id, node);

        for (int i = 0; i < node.count; ++i) {
            float child_dist = node.entries[i].mbr.MinDistSq(query_pt);
            if (node.is_leaf)
                pq.push({child_dist, node.entries[i].child_or_record, false});
            else
                pq.push({child_dist, node.entries[i].child_or_record, true});
        }
    }

    return results;
}

int RTree::GetTreeHeight() const
{
    int height = 0;
    int pid    = rtf_.GetRootPageId();
    while (true) {
        RTreeNode node;
        rtf_.ReadNode(pid, node);
        ++height;
        if (node.is_leaf || node.count == 0) break;
        pid = node.entries[0].child_or_record;
    }
    return height;
}
