#include "data_file.h"
#include <cstring>
#include <stdexcept>

DataFile::DataFile(DiskManager* dm, int vector_dim) {
    disk_manager_ = dm;
    vector_dim_ = vector_dim;
    record_size_ = vector_dim_ * sizeof(float);
    records_per_page_ = (PAGE_SIZE - sizeof(int)) / record_size_;

    if (disk_manager_->GetNumPages() == 0) {
        // Brand new file — allocate header page
        disk_manager_->AllocatePage();  // page 0 = header
        total_records_ = 0;
        total_data_pages_ = 0;
        WriteHeader();
    } else {
        // Existing file — read state from header page
        LoadHeader();
    }
}

void DataFile::LoadHeader() {
    Page p;
    disk_manager_->ReadPage(0, p.data_);
    // Header layout: [ total_records (4 bytes) | total_data_pages (4 bytes) ]
    std::memcpy(&total_records_,    p.data_,                 sizeof(int));
    std::memcpy(&total_data_pages_, p.data_ + sizeof(int),   sizeof(int));
}

void DataFile::WriteHeader() {
    Page p;
    std::memcpy(p.data_,               &total_records_,    sizeof(int));
    std::memcpy(p.data_ + sizeof(int), &total_data_pages_, sizeof(int));
    disk_manager_->WritePage(0, p.data_);
}

int DataFile::AppendRecord(const std::vector<float>& vec) {
    // 1. Figure out which data page this record goes on
    int record_id = total_records_;
    int data_page_index = record_id / records_per_page_;  // 0-based data page
    int slot_index      = record_id % records_per_page_;

    // 2. If we need a new page, allocate it
    if (data_page_index >= total_data_pages_) {
        disk_manager_->AllocatePage();  // page 0 is header, so data pages start at page 1
        total_data_pages_++;
    }

    // 3. Real page_id on disk = data_page_index + 1  (page 0 is header)
    int real_page_id = data_page_index + 1;

    // 4. Read that page, write the vector into the right slot
    Page p;
    disk_manager_->ReadPage(real_page_id, p.data_);

    int byte_offset = sizeof(int) + (slot_index * record_size_);
    std::memcpy(p.data_ + byte_offset, vec.data(), record_size_);

    // 5. Update the per-page record count in its header
    int page_record_count = slot_index + 1;
    std::memcpy(p.data_, &page_record_count, sizeof(int));

    // 6. Write page back to disk
    disk_manager_->WritePage(real_page_id, p.data_);

    // 7. Update global state and persist header
    total_records_++;
    WriteHeader();

    return record_id;
}

std::vector<float> DataFile::ReadRecord(int record_id) {
    if (record_id < 0 || record_id >= total_records_) {
        throw std::out_of_range("DataFile: Invalid record_id");
    }

    int data_page_index = record_id / records_per_page_;
    int slot_index      = record_id % records_per_page_;
    int real_page_id    = data_page_index + 1;  // page 0 is header

    Page p;
    disk_manager_->ReadPage(real_page_id, p.data_);

    int byte_offset = sizeof(int) + (slot_index * record_size_);
    std::vector<float> result(vector_dim_);
    std::memcpy(result.data(), p.data_ + byte_offset, record_size_);

    return result;
}
/*

---

## What Changed From Before

| Thing | Before | Now |
|---|---|---|
| `ReadRecord` | Assumed page 0 = first data page | Correctly skips header, data starts at page 1 |
| `AppendRecord` | Returned -1 | Fully working |
| Header tracking | Nothing | Page 0 stores total_records + total_data_pages |
| Bounds check on read | None | Throws if record_id invalid |

---

## Yes — After This, Tree Logic is Next

Your complete foundation is now:
```
DiskManager  ✅  reads/writes raw pages
DataFile     ✅  stores vectors, returns record_id
KDTree       ← next: insert, kNN, range search
*/