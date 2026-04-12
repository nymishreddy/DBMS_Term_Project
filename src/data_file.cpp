#include "data_file.h"
#include <cstring>
#include <stdexcept>

DataFile::DataFile(DiskManager* dm, BufferPoolManager* bpm, int vector_dim) {
    disk_manager_    = dm;
    bpm_             = bpm;
    vector_dim_      = vector_dim;
    record_size_     = vector_dim_ * sizeof(float);
    records_per_page_ = (PAGE_SIZE - sizeof(int)) / record_size_;

    if (disk_manager_->GetNumPages() == 0) {
        int hdr_pid;
        char* hdr = bpm_->NewPage(&hdr_pid); // page 0 = header
        bpm_->UnpinPage(hdr_pid, false);
        total_records_    = 0;
        total_data_pages_ = 0;
        WriteHeader();
    } else {
        LoadHeader();
    }
}

void DataFile::LoadHeader() {
    char* data = bpm_->FetchPage(0);
    std::memcpy(&total_records_,    data,              sizeof(int));
    std::memcpy(&total_data_pages_, data + sizeof(int), sizeof(int));
    bpm_->UnpinPage(0, false);
}

void DataFile::WriteHeader() {
    char* data = bpm_->FetchPage(0);
    std::memcpy(data,               &total_records_,    sizeof(int));
    std::memcpy(data + sizeof(int), &total_data_pages_, sizeof(int));
    bpm_->UnpinPage(0, true);
}

int DataFile::AppendRecord(const std::vector<float>& vec) {
    int record_id       = total_records_;
    int data_page_index = record_id / records_per_page_;
    int slot_index      = record_id % records_per_page_;

    int real_page_id;
    if (data_page_index >= total_data_pages_) {
        char* pg = bpm_->NewPage(&real_page_id);
        bpm_->UnpinPage(real_page_id, false);
        total_data_pages_++;
    } else {
        real_page_id = data_page_index + 1;
    }

    real_page_id = data_page_index + 1;

    char* data       = bpm_->FetchPage(real_page_id);
    int byte_offset  = sizeof(int) + (slot_index * record_size_);
    std::memcpy(data + byte_offset, vec.data(), record_size_);

    int page_record_count = slot_index + 1;
    std::memcpy(data, &page_record_count, sizeof(int));
    bpm_->UnpinPage(real_page_id, true);

    total_records_++;
    WriteHeader();

    return record_id;
}

std::vector<float> DataFile::ReadRecord(int record_id) {
    if (record_id < 0 || record_id >= total_records_)
        throw std::out_of_range("DataFile: Invalid record_id");

    int data_page_index = record_id / records_per_page_;
    int slot_index      = record_id % records_per_page_;
    int real_page_id    = data_page_index + 1;

    char* data      = bpm_->FetchPage(real_page_id);
    int byte_offset = sizeof(int) + (slot_index * record_size_);

    std::vector<float> result(vector_dim_);
    std::memcpy(result.data(), data + byte_offset, record_size_);
    bpm_->UnpinPage(real_page_id, false);

    return result;
}
