#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <string>
#include <fstream>
#include "page.h"

class DiskManager {
private:
    std::fstream db_io_;      
    std::string file_name_;   
    int num_pages_;           

public:
    DiskManager(const std::string& file_name);
    ~DiskManager();

    int AllocatePage();
    void ReadPage(int page_id, char* page_data);
    void WritePage(int page_id, const char* page_data);
    
    int GetNumPages() const;
};

#endif