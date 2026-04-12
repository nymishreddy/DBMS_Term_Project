#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include "disk_manager.h"
#include "page.h"
#include <unordered_map>
#include <list>
#include <vector>
#include <stdexcept>

struct Frame {
    char data[PAGE_SIZE];
    int  page_id    = -1;
    int  pin_count  = 0;
    bool is_dirty   = false;
};

class LRUReplacer {
public:
    explicit LRUReplacer(int capacity) : capacity_(capacity) {}

    // Record a frame as unpinned and eligible for eviction
    void Insert(int frame_id) {
        if (map_.count(frame_id)) return;
        lru_.push_front(frame_id);
        map_[frame_id] = lru_.begin();
    }

    // Remove a frame from eviction candidates (it's being pinned)
    void Remove(int frame_id) {
        auto it = map_.find(frame_id);
        if (it == map_.end()) return;
        lru_.erase(it->second);
        map_.erase(it);
    }

    // Evict the LRU frame; returns frame_id or -1 if none available
    int Evict() {
        if (lru_.empty()) return -1;
        int fid = lru_.back();
        lru_.pop_back();
        map_.erase(fid);
        return fid;
    }

    int Size() const { return static_cast<int>(lru_.size()); }

private:
    int capacity_;
    std::list<int> lru_;
    std::unordered_map<int, std::list<int>::iterator> map_;
};


class BufferPoolManager {
public:
    BufferPoolManager(DiskManager* dm, int pool_size);
    ~BufferPoolManager();

    // Pin a page into the pool; returns pointer to its data buffer.
    // Caller MUST call UnpinPage when done.
    char* FetchPage(int page_id);

    // Allocate a new disk page, bring it into the pool pinned.
    // Writes the assigned page_id into *page_id_out.
    char* NewPage(int* page_id_out);

    // Release the caller's pin. Set is_dirty=true if the page was modified.
    void UnpinPage(int page_id, bool is_dirty);

    // Force a page to disk immediately (does not evict).
    void FlushPage(int page_id);

    // Flush every dirty page to disk.
    void FlushAll();

private:
    DiskManager* dm_;
    int pool_size_;
    std::vector<Frame> frames_;
    std::unordered_map<int, int> page_table_; // page_id -> frame_id
    LRUReplacer replacer_;
    std::list<int> free_list_; // frame_ids not yet used

    // Find a victim frame (from free list, then LRU). Flushes if dirty.
    // Returns frame_id, or throws if pool is exhausted.
    int GetFreeFrame();
};

#endif // BUFFER_POOL_MANAGER_H
