#pragma once
#include "types.h"

struct VfsDriveInfo {
    bool present;
    char letter;
    uint32_t disk_index;
    int32_t partition_index;
    char filesystem[16];
    char label[16];
};

struct VfsDirEntry {
    bool present;
    bool is_dir;
    uint32_t size;
    char name[64];
};

struct VfsPathInfo {
    bool exists;
    bool is_dir;
    uint32_t size;
};

void vfs_init();
uint32_t vfs_drive_count();
const VfsDriveInfo* vfs_get_drive(uint32_t index);
bool vfs_list_dir(const char* path, VfsDirEntry* entries, uint32_t max_entries, uint32_t* out_count);
bool vfs_read_file(const char* path, void* buffer, uint32_t buffer_size, uint32_t* out_size);
bool vfs_stat(const char* path, VfsPathInfo* out_info);
