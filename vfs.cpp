#include "vfs.h"
#include "storage.h"

struct Fat32DriveMount {
    bool present;
    VfsDriveInfo info;
};

bool fat32_try_mount(uint32_t disk_index, int32_t partition_index, char drive_letter, VfsDriveInfo* out_info);
bool fat32_list_dir(const VfsDriveInfo* drive, const char* subpath, VfsDirEntry* entries, uint32_t max_entries, uint32_t* out_count);
bool fat32_read_file(const VfsDriveInfo* drive, const char* subpath, void* buffer, uint32_t buffer_size, uint32_t* out_size);
bool fat32_stat_path(const VfsDriveInfo* drive, const char* subpath, VfsPathInfo* out_info);

static Fat32DriveMount g_drives[8];
static uint32_t g_drive_count = 0;

static void copy_cstr(char* dst, const char* src, uint32_t cap) {
    if (cap == 0) return;
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool str_eq_ci(char a, char b) {
    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    return a == b;
}

static const VfsDriveInfo* find_drive_by_letter(char letter) {
    for (uint32_t i = 0; i < g_drive_count; i++) {
        if (!g_drives[i].present) continue;
        if (str_eq_ci(g_drives[i].info.letter, letter)) return &g_drives[i].info;
    }
    return nullptr;
}

static const char* parse_drive_path(const char* path, const VfsDriveInfo** out_drive) {
    if (!path || !path[0] || path[1] != ':') return nullptr;
    const VfsDriveInfo* drive = find_drive_by_letter(path[0]);
    if (!drive) return nullptr;
    const char* subpath = path + 2;
    while (*subpath == '\\' || *subpath == '/') subpath++;
    *out_drive = drive;
    return subpath;
}

void vfs_init() {
    for (uint32_t i = 0; i < 8; i++) {
        g_drives[i].present = false;
    }
    g_drive_count = 0;

    char next_letter = 'C';
    uint32_t disk_count = storage_disk_count();
    for (uint32_t disk_index = 0; disk_index < disk_count && g_drive_count < 8; disk_index++) {
        const DiskInfo* disk = storage_get_disk(disk_index);
        if (!disk || !disk->present) continue;

        if (disk->partition_count == 0) {
            VfsDriveInfo info = {};
            if (fat32_try_mount(disk_index, -1, next_letter, &info)) {
                g_drives[g_drive_count].present = true;
                g_drives[g_drive_count].info = info;
                g_drive_count++;
                next_letter++;
            }
            continue;
        }

        for (uint32_t part_index = 0; part_index < disk->partition_count && g_drive_count < 8; part_index++) {
            if (!disk->partitions[part_index].present) continue;
            VfsDriveInfo info = {};
            if (fat32_try_mount(disk_index, (int32_t)part_index, next_letter, &info)) {
                g_drives[g_drive_count].present = true;
                g_drives[g_drive_count].info = info;
                g_drive_count++;
                next_letter++;
            }
        }
    }
}

uint32_t vfs_drive_count() {
    return g_drive_count;
}

const VfsDriveInfo* vfs_get_drive(uint32_t index) {
    if (index >= g_drive_count) return nullptr;
    return &g_drives[index].info;
}

bool vfs_list_dir(const char* path, VfsDirEntry* entries, uint32_t max_entries, uint32_t* out_count) {
    const VfsDriveInfo* drive = nullptr;
    const char* subpath = parse_drive_path(path, &drive);
    if (!subpath || !drive) return false;
    if (drive->filesystem[0] == 'F') {
        return fat32_list_dir(drive, subpath, entries, max_entries, out_count);
    }
    return false;
}

bool vfs_read_file(const char* path, void* buffer, uint32_t buffer_size, uint32_t* out_size) {
    const VfsDriveInfo* drive = nullptr;
    const char* subpath = parse_drive_path(path, &drive);
    if (!subpath || !drive) return false;
    if (drive->filesystem[0] == 'F') {
        return fat32_read_file(drive, subpath, buffer, buffer_size, out_size);
    }
    return false;
}

bool vfs_stat(const char* path, VfsPathInfo* out_info) {
    const VfsDriveInfo* drive = nullptr;
    const char* subpath = parse_drive_path(path, &drive);
    if (!subpath || !drive || !out_info) return false;
    if (drive->filesystem[0] == 'F') {
        return fat32_stat_path(drive, subpath, out_info);
    }
    return false;
}
