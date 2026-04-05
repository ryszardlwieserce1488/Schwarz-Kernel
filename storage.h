#pragma once
#include "types.h"

enum PartitionStyle : uint8_t {
    PARTITION_STYLE_NONE = 0,
    PARTITION_STYLE_MBR = 1,
    PARTITION_STYLE_GPT = 2,
};

enum BootFsKind : uint32_t {
    BOOT_FS_UNKNOWN = 0,
    BOOT_FS_FAT12 = 1,
    BOOT_FS_FAT16 = 2,
    BOOT_FS_FAT32 = 3,
};

struct BootVolumeHandoff {
    uint32_t present;
    uint32_t removable;
    uint32_t logical_partition;
    uint32_t fs_kind;
    uint32_t block_size;
    uint32_t sector0_valid;
    uint64_t block_count;
    uint8_t sector0[512];
};

struct DiskPartitionInfo {
    bool present;
    uint64_t first_lba;
    uint64_t sector_count;
    char type_name[32];
    char filesystem[16];
};

struct DiskInfo {
    bool present;
    bool removable;
    uint64_t sector_size;
    uint64_t sector_count;
    PartitionStyle partition_style;
    char name[16];
    char transport[16];
    char filesystem[16];
    uint32_t partition_count;
    DiskPartitionInfo partitions[8];
    uint32_t usb_device_index; // index w g_xhci[0].devices[]
};

struct UnsupportedControllerInfo {
    char name[32];
    char reason[48];
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
};

void storage_init();
void storage_register_boot_volume(const BootVolumeHandoff* handoff);
uint32_t storage_disk_count();
const DiskInfo* storage_get_disk(uint32_t index);
bool storage_read_disk_sector(uint32_t disk_index, uint64_t lba, void* buffer);
bool storage_read_partition_sector(uint32_t disk_index, uint32_t partition_index, uint64_t rel_lba, void* buffer);
const char* storage_partition_style_name(PartitionStyle style);
const char* storage_boot_fs_name(uint32_t fs_kind);
uint32_t storage_unsupported_count();
const UnsupportedControllerInfo* storage_get_unsupported(uint32_t index);
void storage_register_usb_disk(uint32_t usb_device_index, uint64_t sector_count, uint32_t sector_size);