#include "rtree_file.h"
#include <cstring>
#include <stdexcept>

// Header layout in page 0:
//   [4B root_page_id] [4B dim] [4B node_count]

RTreeFile::RTreeFile(DiskManager* dm, BufferPoolManager* bpm, int dim)
    : dm_(dm), bpm_(bpm), dim_(dim), root_page_id_(-1), node_count_(0)
{
    if (dm_->GetNumPages() == 0) {
        int hdr_pid;
        char* hdr = bpm_->NewPage(&hdr_pid); // page 0: header
        bpm_->UnpinPage(hdr_pid, false);

        node_count_ = 0;
        int root_pid = AllocateNode();
        root_page_id_ = root_pid;

        RTreeNode root;
        root.is_leaf  = true;
        root.count    = 0;
        root.page_id  = root_pid;
        WriteNode(root);
        WriteHeader();
    } else {
        LoadHeader();
    }
}

void RTreeFile::LoadHeader() {
    char* data = bpm_->FetchPage(0);
    std::memcpy(&root_page_id_, data,                  sizeof(int));
    std::memcpy(&dim_,          data + sizeof(int),    sizeof(int));
    std::memcpy(&node_count_,   data + 2*sizeof(int),  sizeof(int));
    bpm_->UnpinPage(0, false);
}

void RTreeFile::WriteHeader() {
    char* data = bpm_->FetchPage(0);
    std::memcpy(data,                 &root_page_id_, sizeof(int));
    std::memcpy(data + sizeof(int),   &dim_,          sizeof(int));
    std::memcpy(data + 2*sizeof(int), &node_count_,   sizeof(int));
    bpm_->UnpinPage(0, true);
}

void RTreeFile::ReadNode(int page_id, RTreeNode& node) const {
    char* data = bpm_->FetchPage(page_id);
    node.Deserialize(data, dim_);
    node.page_id = page_id;
    bpm_->UnpinPage(page_id, false);
}

void RTreeFile::WriteNode(const RTreeNode& node) {
    char* data = bpm_->FetchPage(node.page_id);
    node.Serialize(data, dim_);
    bpm_->UnpinPage(node.page_id, true);
}

int RTreeFile::AllocateNode() {
    int pid;
    char* data = bpm_->NewPage(&pid);
    bpm_->UnpinPage(pid, false);
    ++node_count_;
    WriteHeader();
    return pid;
}

void RTreeFile::SetRootPageId(int id) {
    root_page_id_ = id;
    WriteHeader();
}
