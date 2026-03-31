#ifndef RTREE_FILE_H
#define RTREE_FILE_H

#include "disk_manager.h"
#include "rtree_node.h"

// ─────────────────────────────────────────────────────────────────────────────
// RTreeFile — owns a DiskManager and provides RTreeNode read/write on top of it.
//
// Page 0 layout (header):
//   [4B root_page_id] [4B dim] [4B node_count]
//
// Pages 1..N are RTreeNode pages.
// ─────────────────────────────────────────────────────────────────────────────
class RTreeFile {
public:
    RTreeFile(DiskManager* dm, int dim);

    // Load a node from disk into 'node' (fills node.page_id too)
    void ReadNode(int page_id, RTreeNode& node) const;

    // Write a node back to its page
    void WriteNode(const RTreeNode& node);

    // Allocate a fresh blank page, return its page_id
    int AllocateNode();

    // Accessors
    int GetRootPageId()  const { return root_page_id_; }
    int GetDim()         const { return dim_; }
    int GetNodeCount()   const { return node_count_; }

    void SetRootPageId(int id);  // also persists header

private:
    DiskManager* dm_;
    int dim_;
    int root_page_id_;
    int node_count_;

    void LoadHeader();
    void WriteHeader();
};

#endif  // RTREE_FILE_H