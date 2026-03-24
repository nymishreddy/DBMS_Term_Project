#ifndef KD_TREE_PAGE_H
#define KD_TREE_PAGE_H

#include "page.h" 

struct KDNode {
    int split_dim;
    float split_val;
    int left_page_id;
    int right_page_id;
    int record_id;
};

struct KDTreePage {
    // Header
    int current_nodes_;
    int max_nodes_;

    // Data (4080 bytes)
    KDNode nodes_[204]; 

    void Init() {
        current_nodes_ = 0;
        max_nodes_ = 204;
    }

    bool HasSpace() const {
        return current_nodes_ < max_nodes_;
    }

    void InsertNode(const KDNode& new_node) {
        nodes_[current_nodes_] = new_node;
        current_nodes_++;
    }
};

#endif