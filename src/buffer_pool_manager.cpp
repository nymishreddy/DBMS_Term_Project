#include "buffer_pool_manager.h"
#include <cstring>

BufferPoolManager::BufferPoolManager(DiskManager* dm, int pool_size)
    : dm_(dm), pool_size_(pool_size), frames_(pool_size), replacer_(pool_size)
{
    for (int i = 0; i < pool_size_; ++i)
        free_list_.push_back(i);
}

BufferPoolManager::~BufferPoolManager() {
    FlushAll();
}

int BufferPoolManager::GetFreeFrame() {
    int fid = -1;
    if (!free_list_.empty()) {
        fid = free_list_.front();
        free_list_.pop_front();
        return fid;
    }
    fid = replacer_.Evict();
    if (fid == -1)
        throw std::runtime_error("BufferPoolManager: pool exhausted, all pages pinned.");

    Frame& f = frames_[fid];
    if (f.is_dirty) {
        dm_->WritePage(f.page_id, f.data);
        f.is_dirty = false;
    }
    page_table_.erase(f.page_id);
    f.page_id = -1;
    return fid;
}

char* BufferPoolManager::FetchPage(int page_id) {
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        int fid = it->second;
        frames_[fid].pin_count++;
        replacer_.Remove(fid);
        return frames_[fid].data;
    }

    int fid = GetFreeFrame();
    Frame& f = frames_[fid];
    dm_->ReadPage(page_id, f.data);
    f.page_id   = page_id;
    f.pin_count = 1;
    f.is_dirty  = false;
    page_table_[page_id] = fid;
    return f.data;
}

char* BufferPoolManager::NewPage(int* page_id_out) {
    int pid = dm_->AllocatePage();
    *page_id_out = pid;

    int fid = GetFreeFrame();
    Frame& f = frames_[fid];
    std::memset(f.data, 0, PAGE_SIZE);
    f.page_id   = pid;
    f.pin_count = 1;
    f.is_dirty  = true;
    page_table_[pid] = fid;
    return f.data;
}

void BufferPoolManager::UnpinPage(int page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;

    int fid = it->second;
    Frame& f = frames_[fid];
    if (f.pin_count <= 0) return;

    if (is_dirty) f.is_dirty = true;
    f.pin_count--;

    if (f.pin_count == 0)
        replacer_.Insert(fid);
}

void BufferPoolManager::FlushPage(int page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;
    Frame& f = frames_[it->second];
    if (f.is_dirty) {
        dm_->WritePage(f.page_id, f.data);
        f.is_dirty = false;
    }
}

void BufferPoolManager::FlushAll() {
    for (auto& [pid, fid] : page_table_) {
        Frame& f = frames_[fid];
        if (f.is_dirty) {
            dm_->WritePage(f.page_id, f.data);
            f.is_dirty = false;
        }
    }
}
