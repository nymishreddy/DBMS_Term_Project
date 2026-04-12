#ifndef RTREE_FILE_H
#define RTREE_FILE_H

#include "buffer_pool_manager.h"
#include "disk_manager.h"
#include "rtree_node.h"

class RTreeFile {
public:
    RTreeFile(DiskManager* dm, BufferPoolManager* bpm, int dim);

    void ReadNode(int page_id, RTreeNode& node) const;
    void WriteNode(const RTreeNode& node);
    int  AllocateNode();

    int GetRootPageId() const { return root_page_id_; }
    int GetDim()        const { return dim_; }
    int GetNodeCount()  const { return node_count_; }

    void SetRootPageId(int id);

private:
    DiskManager*        dm_;
    BufferPoolManager*  bpm_;
    int dim_;
    int root_page_id_;
    int node_count_;

    void LoadHeader();
    void WriteHeader();
};

#endif // RTREE_FILE_H
