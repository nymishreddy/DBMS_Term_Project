#ifndef PAGE_H
#define PAGE_H

#include <cstring> 

constexpr int PAGE_SIZE = 4096; 

struct Page {
    int page_id_;
    char data_[PAGE_SIZE];

    Page() {
        page_id_ = -1; 
        ResetMemory();
    }

    void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
    }
};

#endif