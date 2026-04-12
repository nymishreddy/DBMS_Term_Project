#include "disk_manager.h"
#include <iostream>
#include <stdexcept>

DiskManager::DiskManager(const std::string& file_name) : file_name_(file_name), num_pages_(0) {
    db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);

    if (!db_io_.is_open()) {
        db_io_.clear(); 
        db_io_.open(file_name_, std::ios::out | std::ios::binary | std::ios::trunc);
        db_io_.close();
        
        db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
        
        if (!db_io_.is_open()) {
            throw std::runtime_error("DiskManager Fatal Error: Could not create or open file.");
        }
    }

    db_io_.seekg(0, std::ios::end);  
    int file_size = db_io_.tellg();  
    num_pages_ = file_size / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

int DiskManager::AllocatePage() {
    int new_page_id = num_pages_;
    char blank_data[PAGE_SIZE] = {0};
    
    db_io_.seekp(new_page_id * PAGE_SIZE, std::ios::beg);
    db_io_.write(blank_data, PAGE_SIZE);
    //db_io_.flush(); 
    
    num_pages_++;
    return new_page_id;
}

void DiskManager::ReadPage(int page_id, char* page_data) {
    if (page_id >= num_pages_ || page_id < 0) {
        throw std::out_of_range("DiskManager: Trying to read an invalid page_id.");
    }

    int offset = page_id * PAGE_SIZE;
    db_io_.seekg(offset, std::ios::beg);
    db_io_.read(page_data, PAGE_SIZE);
    read_count_++;
}

void DiskManager::WritePage(int page_id, const char* page_data) {
    if (page_id >= num_pages_ || page_id < 0) {
        throw std::out_of_range("DiskManager: Trying to write to an unallocated page_id.");
    }

    int offset = page_id * PAGE_SIZE;
    db_io_.seekp(offset, std::ios::beg);
    db_io_.write(page_data, PAGE_SIZE);
    //db_io_.flush();
}

int DiskManager::GetNumPages() const {
    return num_pages_;
}