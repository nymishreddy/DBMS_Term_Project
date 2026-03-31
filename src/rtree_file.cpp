#include "rtree_file.h"
#include <cstring>
#include <stdexcept>

// Header layout in page 0:
//   [4B root_page_id] [4B dim] [4B node_count]

RTreeFile::RTreeFile(DiskManager *dm, int dim)
    : dm_(dm), dim_(dim), root_page_id_(-1), node_count_(0)
{

    if (dm_->GetNumPages() == 0)
    {
        // Brand new file — allocate header page and one root leaf
        dm_->AllocatePage(); // page 0: header
        node_count_ = 0;

        // Allocate root page (page 1) as empty leaf
        int root_pid = AllocateNode();
        root_page_id_ = root_pid;

        RTreeNode root;
        root.is_leaf = true;
        root.count = 0;
        root.page_id = root_pid;
        WriteNode(root);

        WriteHeader();
    }
    else
    {
        LoadHeader();
    }
}

void RTreeFile::LoadHeader()
{
    Page p;
    dm_->ReadPage(0, p.data_);
    std::memcpy(&root_page_id_, p.data_, sizeof(int));
    std::memcpy(&dim_, p.data_ + sizeof(int), sizeof(int));
    std::memcpy(&node_count_, p.data_ + 2 * sizeof(int), sizeof(int));
}

void RTreeFile::WriteHeader()
{
    Page p;
    std::memcpy(p.data_, &root_page_id_, sizeof(int));
    std::memcpy(p.data_ + sizeof(int), &dim_, sizeof(int));
    std::memcpy(p.data_ + 2 * sizeof(int), &node_count_, sizeof(int));
    dm_->WritePage(0, p.data_);
}

void RTreeFile::ReadNode(int page_id, RTreeNode &node) const
{
    Page p;
    dm_->ReadPage(page_id, p.data_);
    node.Deserialize(p.data_, dim_);
    node.page_id = page_id;
}

void RTreeFile::WriteNode(const RTreeNode &node)
{
    Page p;
    node.Serialize(p.data_, dim_);
    dm_->WritePage(node.page_id, p.data_);
}

int RTreeFile::AllocateNode()
{
    int pid = dm_->AllocatePage();
    ++node_count_;
    WriteHeader();
    return pid;
}

void RTreeFile::SetRootPageId(int id)
{
    root_page_id_ = id;
    WriteHeader();
}