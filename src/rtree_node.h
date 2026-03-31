#ifndef RTREE_NODE_H
#define RTREE_NODE_H

#include "page.h"
#include <vector>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// MBR (Minimum Bounding Rectangle) in d dimensions
// Stored as:  [ min[0], min[1], ..., min[d-1], max[0], ..., max[d-1] ]
// Size in bytes: 2 * d * sizeof(float)
// ─────────────────────────────────────────────────────────────────────────────
struct MBR
{
    std::vector<float> min_vals;
    std::vector<float> max_vals;

    MBR() = default;

    explicit MBR(int dim)
        : min_vals(dim, std::numeric_limits<float>::max()),
          max_vals(dim, -std::numeric_limits<float>::max()) {}

    int Dim() const { return static_cast<int>(min_vals.size()); }

    // Expand this MBR to include a point
    void ExpandToIncludePoint(const std::vector<float> &pt)
    {
        for (int i = 0; i < Dim(); ++i)
        {
            min_vals[i] = std::min(min_vals[i], pt[i]);
            max_vals[i] = std::max(max_vals[i], pt[i]);
        }
    }

    // Expand this MBR to include another MBR
    void ExpandToIncludeMBR(const MBR &other)
    {
        for (int i = 0; i < Dim(); ++i)
        {
            min_vals[i] = std::min(min_vals[i], other.min_vals[i]);
            max_vals[i] = std::max(max_vals[i], other.max_vals[i]);
        }
    }

    // Volume (area in 2D) of this MBR
    float Volume() const
    {
        float vol = 1.0f;
        for (int i = 0; i < Dim(); ++i)
        {
            float span = max_vals[i] - min_vals[i];
            if (span < 0.0f)
                return 0.0f;
            vol *= span;
        }
        return vol;
    }

    // How much does this MBR need to grow to include 'other'?
    float ExpansionNeeded(const MBR &other) const
    {
        MBR enlarged = *this;
        enlarged.ExpandToIncludeMBR(other);
        return enlarged.Volume() - Volume();
    }

    // How much does this MBR need to grow to include point pt?
    float ExpansionNeededForPoint(const std::vector<float> &pt) const
    {
        MBR enlarged = *this;
        enlarged.ExpandToIncludePoint(pt);
        return enlarged.Volume() - Volume();
    }

    // Do two MBRs overlap?
    bool Overlaps(const MBR &other) const
    {
        for (int i = 0; i < Dim(); ++i)
        {
            if (min_vals[i] > other.max_vals[i])
                return false;
            if (max_vals[i] < other.min_vals[i])
                return false;
        }
        return true;
    }

    // Does this MBR contain a point?
    bool ContainsPoint(const std::vector<float> &pt) const
    {
        for (int i = 0; i < Dim(); ++i)
        {
            if (pt[i] < min_vals[i] || pt[i] > max_vals[i])
                return false;
        }
        return true;
    }

    // Minimum squared Euclidean distance from a point to this MBR
    // (MINDIST — used for kNN priority queue ordering)
    float MinDistSq(const std::vector<float> &pt) const
    {
        float dist_sq = 0.0f;
        for (int i = 0; i < Dim(); ++i)
        {
            float r;
            if (pt[i] < min_vals[i])
            {
                r = min_vals[i];
            }
            else if (pt[i] > max_vals[i])
            {
                r = max_vals[i];
            }
            else
            {
                r = pt[i];
            }

            float diff = pt[i] - r;
            dist_sq += diff * diff;
        }
        return dist_sq;
    }

    // Serialize into a byte buffer (must have 2*dim*sizeof(float) bytes)
    void Serialize(char *buf) const
    {
        size_t half = Dim() * sizeof(float);
        std::memcpy(buf, min_vals.data(), half);
        std::memcpy(buf + half, max_vals.data(), half);
    }

    // Deserialize from a byte buffer
    void Deserialize(const char *buf, int dim)
    {
        min_vals.resize(dim);
        max_vals.resize(dim);
        size_t half = dim * sizeof(float);
        std::memcpy(min_vals.data(), buf, half);
        std::memcpy(max_vals.data(), buf + half, half);
    }

    int SerializedSize() const
    {
        return 2 * Dim() * static_cast<int>(sizeof(float));
    }
};

// Build an MBR from a single point
inline MBR MBRFromPoint(const std::vector<float> &pt)
{
    MBR m(static_cast<int>(pt.size()));
    m.min_vals = pt;
    m.max_vals = pt;
    return m;
}

// Union of two MBRs
inline MBR MBRUnion(const MBR &a, const MBR &b)
{
    MBR result = a;
    result.ExpandToIncludeMBR(b);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// RTreeEntry — one slot inside a node
//
//  Leaf node:     mbr = bounding box of the vector,  child_or_record = record_id
//  Internal node: mbr = bounding box of subtree,     child_or_record = child_page_id
// ─────────────────────────────────────────────────────────────────────────────
struct RTreeEntry
{
    MBR mbr;
    int child_or_record; // record_id (leaf) or page_id (internal)

    RTreeEntry() : child_or_record(-1) {}
    explicit RTreeEntry(int dim) : mbr(dim), child_or_record(-1) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// RTreeNode — the in-memory representation of one R-tree page
//
// Page layout  (all packed from offset 0):
//   [4B is_leaf] [4B count] [count × entry_size bytes]
//
// entry_size = mbr_size + 4  = 2*dim*sizeof(float) + sizeof(int)
//
// Max entries (M) is computed once from dim and PAGE_SIZE at startup.
// ─────────────────────────────────────────────────────────────────────────────
class RTreeNode
{
public:
    bool is_leaf;
    int count;
    int page_id; // which page this node lives on (set by RTreeFile)
    std::vector<RTreeEntry> entries;

    RTreeNode() : is_leaf(true), count(0), page_id(-1) {}

    // ── Capacity math ────────────────────────────────────────────────────────
    // Returns the max number of entries that fit in one PAGE_SIZE page.
    static int MaxEntries(int dim)
    {
        int mbr_bytes = 2 * dim * static_cast<int>(sizeof(float));
        int entry_bytes = mbr_bytes + static_cast<int>(sizeof(int));
        int header_bytes = 2 * static_cast<int>(sizeof(int)); // is_leaf + count
        return (PAGE_SIZE - header_bytes) / entry_bytes;
    }

    // Minimum fill (Guttman's recommendation: 40% of M)
    static int MinEntries(int dim)
    {
        return std::max(1, MaxEntries(dim) * 2 / 5);
    }

    // ── Serialization ────────────────────────────────────────────────────────
    void Serialize(char *page_data, int dim) const
    {
        int is_leaf_int = is_leaf ? 1 : 0;
        std::memcpy(page_data, &is_leaf_int, sizeof(int));
        std::memcpy(page_data + sizeof(int), &count, sizeof(int));

        int mbr_bytes = 2 * dim * static_cast<int>(sizeof(float));
        int entry_bytes = mbr_bytes + static_cast<int>(sizeof(int));
        int offset = 2 * static_cast<int>(sizeof(int));

        for (int i = 0; i < count; ++i)
        {
            const RTreeEntry &e = entries[i];
            e.mbr.Serialize(page_data + offset);
            std::memcpy(page_data + offset + mbr_bytes,
                        &e.child_or_record, sizeof(int));
            offset += entry_bytes;
        }
    }

    void Deserialize(const char *page_data, int dim)
    {
        int is_leaf_int = 0;
        std::memcpy(&is_leaf_int, page_data, sizeof(int));
        std::memcpy(&count, page_data + sizeof(int), sizeof(int));
        is_leaf = (is_leaf_int != 0);

        int mbr_bytes = 2 * dim * static_cast<int>(sizeof(float));
        int entry_bytes = mbr_bytes + static_cast<int>(sizeof(int));
        int offset = 2 * static_cast<int>(sizeof(int));

        entries.resize(count);
        for (int i = 0; i < count; ++i)
        {
            entries[i].mbr.Deserialize(page_data + offset, dim);
            std::memcpy(&entries[i].child_or_record,
                        page_data + offset + mbr_bytes, sizeof(int));
            offset += entry_bytes;
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────────────
    bool IsFull(int dim) const
    {
        return count >= MaxEntries(dim);
    }

    // Recompute bounding MBR over all entries in this node
    MBR ComputeBoundingMBR() const
    {
        if (count == 0)
            return MBR(entries.empty() ? 0 : entries[0].mbr.Dim());
        MBR result = entries[0].mbr;
        for (int i = 1; i < count; ++i)
        {
            result.ExpandToIncludeMBR(entries[i].mbr);
        }
        return result;
    }

    void AddEntry(const RTreeEntry &e)
    {
        entries.push_back(e);
        ++count;
    }

    void RemoveEntry(int idx)
    {
        entries.erase(entries.begin() + idx);
        --count;
    }
};

#endif // RTREE_NODE_H