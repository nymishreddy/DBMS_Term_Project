#ifndef DATA_FILE_H
#define DATA_FILE_H

#include "disk_manager.h"
#include "buffer_pool_manager.h"
#include <vector>

class DataFile {
private:
    DiskManager*       disk_manager_;
    BufferPoolManager* bpm_;
    int vector_dim_;
    int record_size_;
    int records_per_page_;
    int total_records_;
    int total_data_pages_;

    void LoadHeader();
    void WriteHeader();

public:
    DataFile(DiskManager* dm, BufferPoolManager* bpm, int vector_dim);

    std::vector<float> ReadRecord(int record_id);
    int AppendRecord(const std::vector<float>& vec);

    int GetTotalRecords() const { return total_records_; }
};

#endif
