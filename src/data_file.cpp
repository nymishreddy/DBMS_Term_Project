#include "data_file.h"
#include <cstring>
#include <stdexcept>

DataFile::DataFile(DiskManager* dm, int vector_dim) {
    disk_manager_ = dm;
    vector_dim_ = vector_dim;
    record_size_ = vector_dim_ * sizeof(float);
    
    // Calculate max records we can fit after leaving 4 bytes for the header
    records_per_page_ = (PAGE_SIZE - sizeof(int)) / record_size_; 
}

std::vector<float> DataFile::ReadRecord(int record_id) {
    int target_page_id = record_id / records_per_page_;
    int slot_index = record_id % records_per_page_;

    Page raw_page;
    disk_manager_->ReadPage(target_page_id, raw_page.data_);

    // Skip the 4-byte header, then jump to the right slot
    int byte_offset = sizeof(int) + (slot_index * record_size_);
    
    std::vector<float> result(vector_dim_);
    std::memcpy(result.data(), raw_page.data_ + byte_offset, record_size_);

    return result;
}

int DataFile::AppendRecord(const std::vector<float>& vec) {
    // TODO: We will build the insertion logic here next!
    return -1;
}