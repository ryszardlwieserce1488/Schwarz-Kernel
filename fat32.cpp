#include "vfs.h"
#include "storage.h"

struct Fat32BootSector {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed));

struct FatDirEntry83 {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed));

struct FatLongDirEntry {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
} __attribute__((packed));

struct Fat32Mount {
    bool present;
    uint32_t disk_index;
    int32_t partition_index;
    uint64_t base_lba;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sector_count;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint64_t fat_start_lba;
    uint64_t data_start_lba;
};

static Fat32Mount g_mounts[8];

static void mem_zero(void* dst, uint32_t size) {
    uint8_t* p = (uint8_t*)dst;
    while (size--) *p++ = 0;
}

static void copy_cstr(char* dst, const char* src, uint32_t cap) {
    if (cap == 0) return;
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static bool read_volume_sector(const Fat32Mount* mount, uint64_t rel_lba, void* buffer) {
    if (mount->partition_index >= 0) {
        return storage_read_partition_sector(mount->disk_index, (uint32_t)mount->partition_index, rel_lba, buffer);
    }
    return storage_read_disk_sector(mount->disk_index, mount->base_lba + rel_lba, buffer);
}

static uint32_t first_sector_of_cluster(const Fat32Mount* mount, uint32_t cluster) {
    return (uint32_t)(mount->data_start_lba + ((uint64_t)(cluster - 2) * mount->sectors_per_cluster));
}

static bool read_fat_entry(const Fat32Mount* mount, uint32_t cluster, uint32_t* out_next) {
    alignas(512) alignas(512) uint8_t sector[512];
    uint64_t fat_offset = (uint64_t)cluster * 4;
    uint64_t sector_lba = mount->fat_start_lba + (fat_offset / mount->bytes_per_sector);
    uint32_t offset = (uint32_t)(fat_offset % mount->bytes_per_sector);
    if (!read_volume_sector(mount, sector_lba, sector)) return false;
    uint32_t value = (uint32_t)sector[offset]
        | ((uint32_t)sector[offset + 1] << 8)
        | ((uint32_t)sector[offset + 2] << 16)
        | ((uint32_t)sector[offset + 3] << 24);
    *out_next = value & 0x0FFFFFFFu;
    return true;
}

static bool is_end_of_chain(uint32_t cluster) {
    return cluster >= 0x0FFFFFF8u;
}

static bool is_lfn_entry(const FatDirEntry83* entry) {
    return entry->attr == 0x0F;
}

static void trim_spaces(char* text) {
    int end = 0;
    while (text[end]) end++;
    while (end > 0 && text[end - 1] == ' ') {
        text[end - 1] = '\0';
        end--;
    }
}

static void format_short_name(const FatDirEntry83* entry, char* out, uint32_t cap) {
    char base[9];
    char ext[4];
    for (int i = 0; i < 8; i++) base[i] = (char)entry->name[i];
    base[8] = '\0';
    for (int i = 0; i < 3; i++) ext[i] = (char)entry->name[8 + i];
    ext[3] = '\0';
    trim_spaces(base);
    trim_spaces(ext);

    uint32_t pos = 0;
    out[0] = '\0';
    for (uint32_t i = 0; base[i] && pos + 1 < cap; i++) out[pos++] = base[i];
    if (ext[0] && pos + 1 < cap) {
        out[pos++] = '.';
        for (uint32_t i = 0; ext[i] && pos + 1 < cap; i++) out[pos++] = ext[i];
    }
    out[pos] = '\0';
}

static void append_lfn_chars(char* out, uint32_t cap, uint32_t base, const uint16_t* src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint16_t ch = src[i];
        if (ch == 0x0000 || ch == 0xFFFF) return;
        if (base + i + 1 >= cap) return;
        out[base + i] = (ch <= 0x00FFu) ? (char)ch : '?';
        out[base + i + 1] = '\0';
    }
}

static void decode_lfn_entry(const FatLongDirEntry* lfn, char* out, uint32_t cap) {
    uint32_t seq = (uint32_t)((lfn->order & 0x1F) - 1u);
    uint32_t base = seq * 13u;
    append_lfn_chars(out, cap, base + 0, lfn->name1, 5);
    append_lfn_chars(out, cap, base + 5, lfn->name2, 6);
    append_lfn_chars(out, cap, base + 11, lfn->name3, 2);
}

static void clear_lfn_state(char* long_name) {
    long_name[0] = '\0';
}

static bool short_name_equals(const FatDirEntry83* entry, const char* component) {
    char formatted[64];
    format_short_name(entry, formatted, sizeof(formatted));

    uint32_t i = 0;
    while (formatted[i] && component[i]) {
        if (upper_ascii(formatted[i]) != upper_ascii(component[i])) return false;
        i++;
    }
    return formatted[i] == '\0' && component[i] == '\0';
}

static bool str_eq_ci(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (upper_ascii(a[i]) != upper_ascii(b[i])) return false;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t entry_cluster(const FatDirEntry83* entry) {
    return ((uint32_t)entry->first_cluster_hi << 16) | (uint32_t)entry->first_cluster_lo;
}

static bool entry_name_equals(const FatDirEntry83* entry, const char* long_name, const char* component) {
    if (long_name[0] != '\0') return str_eq_ci(long_name, component);
    return short_name_equals(entry, component);
}

static void entry_name_to_text(const FatDirEntry83* entry, const char* long_name, char* out, uint32_t cap) {
    if (long_name[0] != '\0') copy_cstr(out, long_name, cap);
    else format_short_name(entry, out, cap);
}

static bool next_component(const char** path, char* out, uint32_t cap) {
    while (**path == '\\' || **path == '/') (*path)++;
    if (**path == '\0') return false;

    uint32_t pos = 0;
    while (**path && **path != '\\' && **path != '/') {
        if (pos + 1 < cap) out[pos++] = **path;
        (*path)++;
    }
    out[pos] = '\0';
    while (**path == '\\' || **path == '/') (*path)++;
    return pos > 0;
}

static bool find_in_directory(const Fat32Mount* mount, uint32_t start_cluster, const char* component, FatDirEntry83* out_entry) {
    alignas(512) alignas(512) uint8_t sector[512];
    uint32_t cluster = start_cluster;
    char long_name[256];
    clear_lfn_state(long_name);
    while (cluster >= 2 && !is_end_of_chain(cluster)) {
        uint32_t first_sector = first_sector_of_cluster(mount, cluster);
        for (uint32_t s = 0; s < mount->sectors_per_cluster; s++) {
            if (!read_volume_sector(mount, first_sector + s, sector)) return false;
            for (uint32_t off = 0; off < 512; off += 32) {
                const FatDirEntry83* entry = (const FatDirEntry83*)(sector + off);
                if (entry->name[0] == 0x00) return false;
                if (entry->name[0] == 0xE5) { clear_lfn_state(long_name); continue; }
                if (is_lfn_entry(entry)) {
                    decode_lfn_entry((const FatLongDirEntry*)entry, long_name, sizeof(long_name));
                    continue;
                }
                if (entry->attr & 0x08) { clear_lfn_state(long_name); continue; }
                if (entry_name_equals(entry, long_name, component)) {
                    *out_entry = *entry;
                    return true;
                }
                clear_lfn_state(long_name);
            }
        }
        uint32_t next = 0;
        if (!read_fat_entry(mount, cluster, &next)) return false;
        cluster = next;
    }
    return false;
}

static bool resolve_path(const Fat32Mount* mount, const char* subpath, FatDirEntry83* out_entry, bool* out_is_root) {
    const char* cursor = subpath;
    char component[64];
    uint32_t current_cluster = mount->root_cluster;
    bool have_component = false;
    FatDirEntry83 current_entry = {};

    while (next_component(&cursor, component, sizeof(component))) {
        have_component = true;
        if (!find_in_directory(mount, current_cluster, component, &current_entry)) return false;
        current_cluster = entry_cluster(&current_entry);
    }

    if (!have_component) {
        if (out_is_root) *out_is_root = true;
        mem_zero(out_entry, sizeof(FatDirEntry83));
        return true;
    }

    if (out_is_root) *out_is_root = false;
    *out_entry = current_entry;
    return true;
}

static const Fat32Mount* mount_for_drive(const VfsDriveInfo* drive) {
    if (!drive) return nullptr;
    int idx = drive->letter - 'C';
    if (idx < 0 || idx >= 8) return nullptr;
    if (!g_mounts[idx].present) return nullptr;
    return &g_mounts[idx];
}

bool fat32_try_mount(uint32_t disk_index, int32_t partition_index, char drive_letter, VfsDriveInfo* out_info) {
    alignas(512) alignas(512) uint8_t sector[512];
    bool ok = false;
    uint64_t base_lba = 0;

    if (partition_index >= 0) {
        const DiskInfo* disk = storage_get_disk(disk_index);
        if (!disk) return false;
        if ((uint32_t)partition_index >= disk->partition_count) return false;
        if (!disk->partitions[partition_index].present) return false;
        if (disk->partitions[partition_index].filesystem[0] != 'F') return false;
        ok = storage_read_partition_sector(disk_index, (uint32_t)partition_index, 0, sector);
        base_lba = disk->partitions[partition_index].first_lba;
    } else {
        const DiskInfo* disk = storage_get_disk(disk_index);
        if (!disk) return false;
        if (disk->filesystem[0] != 'F') return false;
        ok = storage_read_disk_sector(disk_index, 0, sector);
    }

    if (!ok) return false;
    const Fat32BootSector* bpb = (const Fat32BootSector*)sector;
    if (bpb->bytes_per_sector != 512) return false;
    if (bpb->fat_size_32 == 0 || bpb->root_cluster < 2 || bpb->sectors_per_cluster == 0) return false;

    int mount_index = drive_letter - 'C';
    if (mount_index < 0 || mount_index >= 8) return false;

    Fat32Mount* mount = &g_mounts[mount_index];
    mem_zero(mount, sizeof(Fat32Mount));
    mount->present = true;
    mount->disk_index = disk_index;
    mount->partition_index = partition_index;
    mount->base_lba = base_lba;
    mount->bytes_per_sector = bpb->bytes_per_sector;
    mount->sectors_per_cluster = bpb->sectors_per_cluster;
    mount->reserved_sector_count = bpb->reserved_sector_count;
    mount->fat_count = bpb->fat_count;
    mount->sectors_per_fat = bpb->fat_size_32;
    mount->root_cluster = bpb->root_cluster;
    mount->fat_start_lba = bpb->reserved_sector_count;
    mount->data_start_lba = bpb->reserved_sector_count + ((uint64_t)bpb->fat_count * bpb->fat_size_32);

    mem_zero(out_info, sizeof(VfsDriveInfo));
    out_info->present = true;
    out_info->letter = drive_letter;
    out_info->disk_index = disk_index;
    out_info->partition_index = partition_index;
    copy_cstr(out_info->filesystem, "FAT32", sizeof(out_info->filesystem));

    char label[12];
    for (int i = 0; i < 11; i++) label[i] = (char)bpb->volume_label[i];
    label[11] = '\0';
    trim_spaces(label);
    if (label[0] == '\0') copy_cstr(out_info->label, "NO_LABEL", sizeof(out_info->label));
    else copy_cstr(out_info->label, label, sizeof(out_info->label));
    return true;
}

bool fat32_list_dir(const VfsDriveInfo* drive, const char* subpath, VfsDirEntry* entries, uint32_t max_entries, uint32_t* out_count) {
    if (out_count) *out_count = 0;
    const Fat32Mount* mount = mount_for_drive(drive);
    if (!mount || !entries || max_entries == 0) return false;

    FatDirEntry83 resolved = {};
    bool is_root = false;
    if (!resolve_path(mount, subpath, &resolved, &is_root)) return false;

    uint32_t dir_cluster = is_root ? mount->root_cluster : entry_cluster(&resolved);
    if (!is_root && (resolved.attr & 0x10) == 0) return false;

    alignas(512) alignas(512) uint8_t sector[512];
    uint32_t cluster = dir_cluster;
    uint32_t count = 0;
    char long_name[256];
    clear_lfn_state(long_name);

    while (cluster >= 2 && !is_end_of_chain(cluster)) {
        uint32_t first_sector = first_sector_of_cluster(mount, cluster);
        for (uint32_t s = 0; s < mount->sectors_per_cluster; s++) {
            if (!read_volume_sector(mount, first_sector + s, sector)) return false;
            for (uint32_t off = 0; off < 512; off += 32) {
                const FatDirEntry83* entry = (const FatDirEntry83*)(sector + off);
                if (entry->name[0] == 0x00) {
                    if (out_count) *out_count = count;
                    return true;
                }
                if (entry->name[0] == 0xE5) { clear_lfn_state(long_name); continue; }
                if (is_lfn_entry(entry)) {
                    decode_lfn_entry((const FatLongDirEntry*)entry, long_name, sizeof(long_name));
                    continue;
                }
                if (entry->attr & 0x08) { clear_lfn_state(long_name); continue; }
                if (count >= max_entries) {
                    if (out_count) *out_count = count;
                    return true;
                }
                entries[count].present = true;
                entries[count].is_dir = (entry->attr & 0x10) != 0;
                entries[count].size = entry->file_size;
                entry_name_to_text(entry, long_name, entries[count].name, sizeof(entries[count].name));
                count++;
                clear_lfn_state(long_name);
            }
        }
        uint32_t next = 0;
        if (!read_fat_entry(mount, cluster, &next)) return false;
        cluster = next;
    }

    if (out_count) *out_count = count;
    return true;
}

bool fat32_read_file(const VfsDriveInfo* drive, const char* subpath, void* buffer, uint32_t buffer_size, uint32_t* out_size) {
    if (out_size) *out_size = 0;
    const Fat32Mount* mount = mount_for_drive(drive);
    if (!mount || !buffer) return false;

    FatDirEntry83 entry = {};
    bool is_root = false;
    if (!resolve_path(mount, subpath, &entry, &is_root)) return false;
    if (is_root || (entry.attr & 0x10) != 0) return false;

    uint32_t file_size = entry.file_size;
    if (file_size > buffer_size) return false;

    uint8_t* out = (uint8_t*)buffer;
    uint32_t written = 0;
    alignas(512) alignas(512) uint8_t sector[512];
    uint32_t cluster = entry_cluster(&entry);

    while (cluster >= 2 && !is_end_of_chain(cluster) && written < file_size) {
        uint32_t first_sector = first_sector_of_cluster(mount, cluster);
        for (uint32_t s = 0; s < mount->sectors_per_cluster && written < file_size; s++) {
            if (!read_volume_sector(mount, first_sector + s, sector)) return false;
            uint32_t to_copy = file_size - written;
            if (to_copy > 512) to_copy = 512;
            for (uint32_t i = 0; i < to_copy; i++) out[written + i] = sector[i];
            written += to_copy;
        }
        uint32_t next = 0;
        if (!read_fat_entry(mount, cluster, &next)) return false;
        cluster = next;
    }

    if (out_size) *out_size = written;
    return written == file_size;
}

bool fat32_stat_path(const VfsDriveInfo* drive, const char* subpath, VfsPathInfo* out_info) {
    if (!out_info) return false;
    out_info->exists = false;
    out_info->is_dir = false;
    out_info->size = 0;

    const Fat32Mount* mount = mount_for_drive(drive);
    if (!mount) return false;

    FatDirEntry83 entry = {};
    bool is_root = false;
    if (!resolve_path(mount, subpath, &entry, &is_root)) return false;

    out_info->exists = true;
    out_info->is_dir = is_root || ((entry.attr & 0x10) != 0);
    out_info->size = is_root ? 0 : entry.file_size;
    return true;
}
