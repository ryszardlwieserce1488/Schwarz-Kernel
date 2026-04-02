#include "storage.h"
#include "memory.h"

extern "C" uint8_t inb(uint16_t port);
extern "C" uint16_t inw(uint16_t port);
extern "C" uint32_t inl(uint16_t port);
extern "C" void outb(uint16_t port, uint8_t value);
extern "C" void outw(uint16_t port, uint16_t value);
extern "C" void outl(uint16_t port, uint32_t value);

static constexpr uint16_t kPciConfigAddress = 0xCF8;
static constexpr uint16_t kPciConfigData = 0xCFC;
static constexpr uint8_t kPciClassMassStorage = 0x01;
static constexpr uint8_t kPciClassSerialBus = 0x0C;
static constexpr uint8_t kPciSubclassUsb = 0x03;
static constexpr uint8_t kPciSubclassIde = 0x01;
static constexpr uint8_t kPciSubclassAhci = 0x06;
static constexpr uint8_t kPciSubclassNvme = 0x08;
static constexpr uint8_t kPciProgIfAhci = 0x01;
static constexpr uint8_t kPciProgIfNvme = 0x02;

static constexpr uint32_t kAhciPortDetPresent = 3;
static constexpr uint32_t kAhciPortIpmActive = 1;
static constexpr uint32_t kHbaPxcmdSt = 1u << 0;
static constexpr uint32_t kHbaPxcmdFre = 1u << 4;
static constexpr uint32_t kHbaPxcmdFr = 1u << 14;
static constexpr uint32_t kHbaPxcmdCr = 1u << 15;
static constexpr uint32_t kHbaPxisTfes = 1u << 30;
static constexpr uint32_t kAtaStatusBusy = 0x80;
static constexpr uint32_t kAtaStatusDrq = 0x08;
static constexpr uint32_t kAtaStatusErr = 0x01;
static constexpr uint8_t kAtaCmdIdentify = 0xEC;
static constexpr uint8_t kAtaCmdReadDmaExt = 0x25;
static constexpr uint8_t kAtaCmdReadPio = 0x20;
static constexpr uint8_t kAtaCmdCacheFlush = 0xE7;
static constexpr uint32_t kSataSigAta = 0x00000101;

static constexpr uint32_t kMaxDisks = 8;
static constexpr uint32_t kMaxPartitionsPerDisk = 8;
static constexpr uint32_t kMaxAhciPorts = 32;
static constexpr uint32_t kAhciCommandSlotCount = 32;
static constexpr uint32_t kSectorSize = 512;
static constexpr uint32_t kMaxUnsupportedControllers = 16;

struct HbaPort {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t rsv0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
};

struct HbaMem {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    volatile uint8_t reserved[0xA0 - 0x2C];
    volatile uint8_t vendor[0x100 - 0xA0];
    HbaPort ports[32];
};

struct HbaPrdtEntry {
    volatile uint32_t dba;
    volatile uint32_t dbau;
    volatile uint32_t reserved0;
    volatile uint32_t dbc : 22;
    volatile uint32_t reserved1 : 9;
    volatile uint32_t i : 1;
};

struct HbaCmdHeader {
    volatile uint8_t cfl : 5;
    volatile uint8_t a : 1;
    volatile uint8_t w : 1;
    volatile uint8_t p : 1;
    volatile uint8_t r : 1;
    volatile uint8_t b : 1;
    volatile uint8_t c : 1;
    volatile uint8_t reserved0 : 1;
    volatile uint8_t pmp : 4;
    volatile uint16_t prdtl;
    volatile uint32_t prdbc;
    volatile uint32_t ctba;
    volatile uint32_t ctbau;
    volatile uint32_t reserved1[4];
};

struct HbaCmdTbl {
    volatile uint8_t cfis[64];
    volatile uint8_t acmd[16];
    volatile uint8_t reserved[48];
    HbaPrdtEntry prdt_entry[1];
};

struct FisRegH2D {
    uint8_t fis_type;
    uint8_t pmport : 4;
    uint8_t reserved0 : 3;
    uint8_t c : 1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved1[4];
} __attribute__((packed));

struct GptHeader {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved0;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_entries_crc32;
} __attribute__((packed));

struct GptPartitionEntry {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name_utf16[36];
} __attribute__((packed));

struct MbrPartitionEntry {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t first_lba;
    uint32_t sector_count;
} __attribute__((packed));

struct AhciDiskDevice {
    HbaPort* port;
    uint8_t port_index;
};

enum DiskBackend : uint8_t {
    DISK_BACKEND_NONE = 0,
    DISK_BACKEND_AHCI = 1,
    DISK_BACKEND_IDE = 2,
    DISK_BACKEND_NVME = 3,
};

struct IdeChannel {
    uint16_t io_base;
    uint16_t control_base;
};

struct IdeDiskDevice {
    IdeChannel channel;
    uint8_t drive_select;
};

struct NvmeSubmissionQueueEntry {
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;
    uint32_t nsid;
    uint64_t reserved2;
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct NvmeCompletionQueueEntry {
    uint32_t command_specific;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed));

struct NvmeQueueState {
    NvmeSubmissionQueueEntry* sq;
    NvmeCompletionQueueEntry* cq;
    uint16_t q_depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t cq_phase;
};

struct NvmeDiskDevice {
    volatile uint8_t* mmio;
    uint32_t doorbell_stride;
    uint32_t namespace_id;
    uint32_t lba_size;
    uint16_t admin_cmd_id;
    uint16_t io_cmd_id;
    NvmeQueueState admin;
    NvmeQueueState io;
};

static DiskInfo g_disks[kMaxDisks];
static DiskBackend g_disk_backends[kMaxDisks];
static AhciDiskDevice g_ahci_devices[kMaxDisks];
static IdeDiskDevice g_ide_devices[kMaxDisks];
static NvmeDiskDevice g_nvme_devices[kMaxDisks];
static UnsupportedControllerInfo g_unsupported[kMaxUnsupportedControllers];
static uint32_t g_disk_count = 0;
static uint32_t g_unsupported_count = 0;
static const char* g_nvme_last_error = "NVMe: inicjalizacja nie powiodla sie";

alignas(1024) static uint8_t g_ahci_cmd_lists[kMaxAhciPorts][1024];
alignas(256) static uint8_t g_ahci_fis[kMaxAhciPorts][256];
alignas(128) static uint8_t g_ahci_cmd_tables[kMaxAhciPorts][kAhciCommandSlotCount][256];
alignas(512) static uint8_t g_sector_buffer[kSectorSize];
alignas(512) static uint8_t g_gpt_entry_buffer[kSectorSize * 8];

static void mem_zero(void* dst, uint64_t size) {
    uint8_t* ptr = (uint8_t*)dst;
    while (size--) {
        *ptr++ = 0;
    }
}

static void mem_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (size--) {
        *d++ = *s++;
    }
}

static void copy_cstr(char* dst, const char* src, uint32_t capacity) {
    if (capacity == 0) return;
    uint32_t i = 0;
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return (a < b) ? a : b;
}

static uint16_t min_u16(uint16_t a, uint16_t b) {
    return (a < b) ? a : b;
}

static void* alloc_aligned(uint64_t size, uint64_t align) {
    if (align < 8) align = 8;
    uint64_t total = size + align + sizeof(uint64_t);
    uint8_t* raw = (uint8_t*)malloc(total);
    if (!raw) return nullptr;
    uintptr_t aligned = ((uintptr_t)(raw + sizeof(uint64_t)) + (align - 1)) & ~(uintptr_t)(align - 1);
    ((uint64_t*)aligned)[-1] = (uint64_t)(uintptr_t)raw;
    mem_zero((void*)aligned, size);
    return (void*)aligned;
}

static uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFC);
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(kPciConfigAddress, pci_config_address(bus, slot, func, offset));
    return inl(kPciConfigData);
}

static uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, func, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

static uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, func, offset);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

static void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint8_t aligned = offset & 0xFC;
    uint32_t current = pci_read32(bus, slot, func, aligned);
    uint32_t shift = (offset & 2) * 8;
    current &= ~(0xFFFFu << shift);
    current |= ((uint32_t)value << shift);
    outl(kPciConfigAddress, pci_config_address(bus, slot, func, aligned));
    outl(kPciConfigData, current);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(kPciConfigAddress, pci_config_address(bus, slot, func, offset & 0xFC));
    outl(kPciConfigData, value);
}

static bool guid_equals(const uint8_t* a, const uint8_t* b) {
    for (uint32_t i = 0; i < 16; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void clear_disk_info(DiskInfo* info) {
    mem_zero(info, sizeof(DiskInfo));
    for (uint32_t i = 0; i < kMaxPartitionsPerDisk; i++) {
        info->partitions[i].present = false;
    }
}

static void set_disk_filesystem(DiskInfo* disk, const char* fs_name) {
    copy_cstr(disk->filesystem, fs_name, sizeof(disk->filesystem));
}

static void set_partition_filesystem(DiskPartitionInfo* part, const char* fs_name) {
    copy_cstr(part->filesystem, fs_name, sizeof(part->filesystem));
}

static void io_delay_400ns(uint16_t alt_status_port) {
    for (int i = 0; i < 4; i++) {
        (void)inb(alt_status_port);
    }
}

static void ide_select_drive(const IdeChannel& channel, uint8_t drive_select) {
    outb(channel.io_base + 6, (uint8_t)(0xA0 | (drive_select << 4)));
    io_delay_400ns(channel.control_base);
}

static bool ide_wait_not_busy(const IdeChannel& channel, uint32_t spin_limit) {
    for (uint32_t spin = 0; spin < spin_limit; spin++) {
        uint8_t status = inb(channel.control_base);
        if ((status & kAtaStatusBusy) == 0) return true;
    }
    return false;
}

static bool ide_wait_drq(const IdeChannel& channel, uint32_t spin_limit) {
    for (uint32_t spin = 0; spin < spin_limit; spin++) {
        uint8_t status = inb(channel.control_base);
        if (status & kAtaStatusErr) return false;
        if ((status & kAtaStatusBusy) == 0 && (status & kAtaStatusDrq) != 0) return true;
    }
    return false;
}

static void clear_unsupported_info(UnsupportedControllerInfo* info) {
    mem_zero(info, sizeof(UnsupportedControllerInfo));
}

static void register_unsupported_controller(uint8_t bus, uint8_t slot, uint8_t func, const char* name, const char* reason) {
    if (g_unsupported_count >= kMaxUnsupportedControllers) return;
    UnsupportedControllerInfo* info = &g_unsupported[g_unsupported_count++];
    clear_unsupported_info(info);
    copy_cstr(info->name, name, sizeof(info->name));
    copy_cstr(info->reason, reason, sizeof(info->reason));
    info->bus = bus;
    info->slot = slot;
    info->func = func;
}

static const char* mass_storage_name(uint8_t subclass, uint8_t prog_if) {
    switch (subclass) {
    case 0x00: return "SCSI storage controller";
    case 0x01: return "IDE controller";
    case 0x04: return "RAID controller";
    case 0x05: return "ATA controller";
    case 0x06: return (prog_if == kPciProgIfAhci) ? "AHCI controller" : "SATA controller";
    case 0x07: return "SAS controller";
    case 0x08: return "NVMe controller";
    default: return "Unknown storage controller";
    }
}

static const char* usb_controller_name(uint8_t prog_if) {
    switch (prog_if) {
    case 0x00: return "UHCI USB controller";
    case 0x10: return "OHCI USB controller";
    case 0x20: return "EHCI USB controller";
    case 0x30: return "xHCI USB controller";
    default: return "USB controller";
    }
}

static const char* detect_fat_boot_sector_kind(const uint8_t* sector0) {
    if (sector0[510] != 0x55 || sector0[511] != 0xAA) return nullptr;

    uint16_t bytes_per_sector = (uint16_t)sector0[11] | ((uint16_t)sector0[12] << 8);
    uint8_t sectors_per_cluster = sector0[13];
    uint16_t reserved_sector_count = (uint16_t)sector0[14] | ((uint16_t)sector0[15] << 8);
    uint8_t num_fats = sector0[16];
    uint16_t root_entry_count = (uint16_t)sector0[17] | ((uint16_t)sector0[18] << 8);
    uint16_t total_sectors_16 = (uint16_t)sector0[19] | ((uint16_t)sector0[20] << 8);
    uint16_t fat_size_16 = (uint16_t)sector0[22] | ((uint16_t)sector0[23] << 8);
    uint32_t total_sectors_32 = (uint32_t)sector0[32] | ((uint32_t)sector0[33] << 8) | ((uint32_t)sector0[34] << 16) | ((uint32_t)sector0[35] << 24);
    uint32_t fat_size_32 = (uint32_t)sector0[36] | ((uint32_t)sector0[37] << 8) | ((uint32_t)sector0[38] << 16) | ((uint32_t)sector0[39] << 24);

    if (bytes_per_sector != 512 && bytes_per_sector != 1024 && bytes_per_sector != 2048 && bytes_per_sector != 4096) return nullptr;
    if (sectors_per_cluster == 0 || reserved_sector_count == 0 || num_fats == 0) return nullptr;

    uint32_t total_sectors = (total_sectors_16 != 0) ? total_sectors_16 : total_sectors_32;
    uint32_t fat_size = (fat_size_16 != 0) ? fat_size_16 : fat_size_32;
    if (total_sectors == 0 || fat_size == 0) return nullptr;

    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32u + (bytes_per_sector - 1)) / bytes_per_sector;
    uint32_t data_sectors = total_sectors - (reserved_sector_count + ((uint32_t)num_fats * fat_size) + root_dir_sectors);
    if (data_sectors == 0) return nullptr;

    uint32_t cluster_count = data_sectors / sectors_per_cluster;
    if (cluster_count < 4085) return "FAT12";
    if (cluster_count < 65525) return "FAT16";
    return "FAT32";
}

static bool extract_sector_count(const uint8_t* identify_data, uint64_t* out_sectors) {
    const uint16_t* words = (const uint16_t*)identify_data;
    uint64_t sectors = 0;
    sectors |= (uint64_t)words[100];
    sectors |= (uint64_t)words[101] << 16;
    sectors |= (uint64_t)words[102] << 32;
    sectors |= (uint64_t)words[103] << 48;
    if (sectors == 0) {
        sectors = (uint64_t)words[60] | ((uint64_t)words[61] << 16);
    }
    *out_sectors = sectors;
    return sectors != 0;
}

static void ahci_stop_cmd(HbaPort* port) {
    port->cmd &= ~kHbaPxcmdSt;
    port->cmd &= ~kHbaPxcmdFre;
    for (uint32_t spin = 0; spin < 1000000; spin++) {
        if ((port->cmd & (kHbaPxcmdFr | kHbaPxcmdCr)) == 0) return;
    }
}

static void ahci_start_cmd(HbaPort* port) {
    while (port->cmd & kHbaPxcmdCr) {
    }
    port->cmd |= kHbaPxcmdFre;
    port->cmd |= kHbaPxcmdSt;
}

static void ahci_rebase_port(HbaPort* port, uint32_t port_index) {
    ahci_stop_cmd(port);

    mem_zero(g_ahci_cmd_lists[port_index], sizeof(g_ahci_cmd_lists[port_index]));
    mem_zero(g_ahci_fis[port_index], sizeof(g_ahci_fis[port_index]));
    mem_zero(g_ahci_cmd_tables[port_index], sizeof(g_ahci_cmd_tables[port_index]));

    uintptr_t cmd_list = (uintptr_t)&g_ahci_cmd_lists[port_index][0];
    uintptr_t fis = (uintptr_t)&g_ahci_fis[port_index][0];

    port->clb = (uint32_t)cmd_list;
    port->clbu = (uint32_t)(cmd_list >> 32);
    port->fb = (uint32_t)fis;
    port->fbu = (uint32_t)(fis >> 32);

    HbaCmdHeader* cmd_header = (HbaCmdHeader*)cmd_list;
    for (uint32_t slot = 0; slot < kAhciCommandSlotCount; slot++) {
        cmd_header[slot].prdtl = 1;
        uintptr_t table = (uintptr_t)&g_ahci_cmd_tables[port_index][slot][0];
        cmd_header[slot].ctba = (uint32_t)table;
        cmd_header[slot].ctbau = (uint32_t)(table >> 32);
    }

    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    ahci_start_cmd(port);
}

static int ahci_find_slot(HbaPort* port) {
    uint32_t slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if ((slots & (1u << i)) == 0) return i;
    }
    return -1;
}

static bool ahci_issue(HbaPort* port, uint32_t port_index, uint8_t command, uint64_t lba, uint32_t sector_count, void* buffer) {
    int slot = ahci_find_slot(port);
    if (slot < 0) return false;

    port->is = 0xFFFFFFFFu;

    HbaCmdHeader* cmd_header = (HbaCmdHeader*)(uintptr_t)port->clb;
    HbaCmdHeader* header = &cmd_header[slot];
    mem_zero((void*)header, sizeof(HbaCmdHeader));
    header->cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
    header->w = 0;
    header->prdtl = 1;

    HbaCmdTbl* cmd_table = (HbaCmdTbl*)(uintptr_t)&g_ahci_cmd_tables[port_index][slot][0];
    mem_zero((void*)cmd_table, sizeof(HbaCmdTbl));
    uintptr_t buf_addr = (uintptr_t)buffer;
    cmd_table->prdt_entry[0].dba = (uint32_t)buf_addr;
    cmd_table->prdt_entry[0].dbau = (uint32_t)(buf_addr >> 32);
    cmd_table->prdt_entry[0].dbc = sector_count * kSectorSize - 1;
    cmd_table->prdt_entry[0].i = 1;

    FisRegH2D* fis = (FisRegH2D*)(&cmd_table->cfis[0]);
    mem_zero(fis, sizeof(FisRegH2D));
    fis->fis_type = 0x27;
    fis->c = 1;
    fis->command = command;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    fis->countl = (uint8_t)(sector_count & 0xFF);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    for (uint32_t spin = 0; spin < 1000000; spin++) {
        uint32_t tfd = port->tfd;
        if ((tfd & (kAtaStatusBusy | kAtaStatusDrq)) == 0) break;
        if (spin == 999999) return false;
    }

    port->ci = 1u << slot;

    for (uint32_t spin = 0; spin < 4000000; spin++) {
        if ((port->ci & (1u << slot)) == 0) break;
        if (port->is & kHbaPxisTfes) return false;
        if (spin == 3999999) return false;
    }

    return (port->is & kHbaPxisTfes) == 0;
}

static bool ahci_identify(HbaPort* port, uint32_t port_index, uint8_t* identify_buffer) {
    mem_zero(identify_buffer, kSectorSize);
    return ahci_issue(port, port_index, kAtaCmdIdentify, 0, 1, identify_buffer);
}

static bool ahci_read_sector(HbaPort* port, uint32_t port_index, uint64_t lba, void* buffer) {
    return ahci_issue(port, port_index, kAtaCmdReadDmaExt, lba, 1, buffer);
}

static bool ide_identify(const IdeChannel& channel, uint8_t drive_select, uint8_t* identify_buffer) {
    mem_zero(identify_buffer, kSectorSize);

    ide_select_drive(channel, drive_select);
    outb(channel.io_base + 1, 0);
    outb(channel.io_base + 2, 0);
    outb(channel.io_base + 3, 0);
    outb(channel.io_base + 4, 0);
    outb(channel.io_base + 5, 0);
    outb(channel.io_base + 7, kAtaCmdIdentify);
    io_delay_400ns(channel.control_base);

    uint8_t status = inb(channel.io_base + 7);
    if (status == 0) return false;

    for (uint32_t spin = 0; spin < 1000000; spin++) {
        status = inb(channel.io_base + 7);
        if (status & kAtaStatusErr) return false;
        if ((status & kAtaStatusBusy) == 0 && (status & kAtaStatusDrq) != 0) break;
        if (spin == 999999) return false;
    }

    uint16_t* out = (uint16_t*)identify_buffer;
    for (uint32_t i = 0; i < 256; i++) {
        out[i] = inw(channel.io_base + 0);
    }
    return true;
}

static bool ide_read_sector(const IdeChannel& channel, uint8_t drive_select, uint64_t lba, void* buffer) {
    if (lba > 0x0FFFFFFFull) return false;
    if (!ide_wait_not_busy(channel, 1000000)) return false;

    outb(channel.control_base, 0);
    outb(channel.io_base + 1, 0);
    outb(channel.io_base + 2, 1);
    outb(channel.io_base + 3, (uint8_t)(lba & 0xFF));
    outb(channel.io_base + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(channel.io_base + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(channel.io_base + 6, (uint8_t)(0xE0 | (drive_select << 4) | ((lba >> 24) & 0x0F)));
    outb(channel.io_base + 7, kAtaCmdReadPio);

    if (!ide_wait_drq(channel, 1000000)) return false;

    uint16_t* out = (uint16_t*)buffer;
    for (uint32_t i = 0; i < 256; i++) {
        out[i] = inw(channel.io_base + 0);
    }

    io_delay_400ns(channel.control_base);
    return true;
}

static volatile uint64_t* nvme_reg64(volatile uint8_t* mmio, uint32_t offset) {
    return (volatile uint64_t*)(mmio + offset);
}

static volatile uint32_t* nvme_reg32(volatile uint8_t* mmio, uint32_t offset) {
    return (volatile uint32_t*)(mmio + offset);
}

static uint32_t nvme_doorbell_offset(const NvmeDiskDevice* dev, uint16_t qid, bool is_cq) {
    uint32_t stride = 4u << dev->doorbell_stride;
    return 0x1000u + ((uint32_t)(qid * 2u + (is_cq ? 1u : 0u)) * stride);
}

static void nvme_ring_sq_doorbell(const NvmeDiskDevice* dev, uint16_t qid, uint16_t tail) {
    *nvme_reg32(dev->mmio, nvme_doorbell_offset(dev, qid, false)) = tail;
}

static void nvme_ring_cq_doorbell(const NvmeDiskDevice* dev, uint16_t qid, uint16_t head) {
    *nvme_reg32(dev->mmio, nvme_doorbell_offset(dev, qid, true)) = head;
}

static bool nvme_wait_csts(volatile uint8_t* mmio, uint32_t mask, uint32_t value) {
    for (uint32_t spin = 0; spin < 4000000; spin++) {
        if ((*nvme_reg32(mmio, 0x1C) & mask) == value) return true;
    }
    return false;
}

static bool nvme_submit_and_wait(NvmeDiskDevice* dev, NvmeQueueState* q, uint16_t qid, NvmeSubmissionQueueEntry* cmd) {
    uint16_t slot = q->sq_tail;
    q->sq[slot] = *cmd;
    q->sq_tail = (uint16_t)((q->sq_tail + 1) % q->q_depth);
    nvme_ring_sq_doorbell(dev, qid, q->sq_tail);

    for (uint32_t spin = 0; spin < 8000000; spin++) {
        NvmeCompletionQueueEntry* cqe = &q->cq[q->cq_head];
        uint8_t phase = (uint8_t)(cqe->status & 1u);
        if (phase != q->cq_phase) continue;
        uint16_t status_code = (uint16_t)((cqe->status >> 1) & 0x7FFu);
        if (cqe->command_id != cmd->command_id || status_code != 0) return false;

        q->cq_head = (uint16_t)((q->cq_head + 1) % q->q_depth);
        if (q->cq_head == 0) q->cq_phase ^= 1;
        nvme_ring_cq_doorbell(dev, qid, q->cq_head);
        return true;
    }
    return false;
}

static bool nvme_identify_controller(NvmeDiskDevice* dev, void* buffer4096) {
    NvmeSubmissionQueueEntry cmd{};
    cmd.opcode = 0x06;
    cmd.command_id = ++dev->admin_cmd_id;
    cmd.prp1 = (uint64_t)(uintptr_t)buffer4096;
    cmd.cdw10 = 1;
    return nvme_submit_and_wait(dev, &dev->admin, 0, &cmd);
}

static bool nvme_identify_namespace(NvmeDiskDevice* dev, uint32_t nsid, void* buffer4096) {
    NvmeSubmissionQueueEntry cmd{};
    cmd.opcode = 0x06;
    cmd.command_id = ++dev->admin_cmd_id;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)buffer4096;
    cmd.cdw10 = 0;
    return nvme_submit_and_wait(dev, &dev->admin, 0, &cmd);
}

static bool nvme_create_io_cq(NvmeDiskDevice* dev, uint16_t qid, uint16_t qsize) {
    NvmeSubmissionQueueEntry cmd{};
    cmd.opcode = 0x05;
    cmd.command_id = ++dev->admin_cmd_id;
    cmd.prp1 = (uint64_t)(uintptr_t)dev->io.cq;
    cmd.cdw10 = ((uint32_t)(qsize - 1) << 16) | qid;
    // PC=1 (physically contiguous), bez wymuszania konkretnego wektora przerwania.
    cmd.cdw11 = 1u;
    return nvme_submit_and_wait(dev, &dev->admin, 0, &cmd);
}

static bool nvme_create_io_sq(NvmeDiskDevice* dev, uint16_t qid, uint16_t qsize) {
    NvmeSubmissionQueueEntry cmd{};
    cmd.opcode = 0x01;
    cmd.command_id = ++dev->admin_cmd_id;
    cmd.prp1 = (uint64_t)(uintptr_t)dev->io.sq;
    cmd.cdw10 = ((uint32_t)(qsize - 1) << 16) | qid;
    // CQID w górnym słowie, QPRIO=0 i PC=1 w dolnym.
    cmd.cdw11 = ((uint32_t)qid << 16) | 1u;
    return nvme_submit_and_wait(dev, &dev->admin, 0, &cmd);
}

static bool nvme_read_one_sector(NvmeDiskDevice* dev, uint64_t lba, void* buffer) {
    NvmeSubmissionQueueEntry cmd{};
    cmd.opcode = 0x02;
    cmd.command_id = ++dev->io_cmd_id;
    cmd.nsid = dev->namespace_id;
    cmd.prp1 = (uint64_t)(uintptr_t)buffer;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = 0;
    return nvme_submit_and_wait(dev, &dev->io, 1, &cmd);
}

static bool nvme_prepare_controller(uint8_t bus, uint8_t slot, uint8_t func, uint64_t mmio_base, NvmeDiskDevice* out_dev, DiskInfo* out_disk) {
    volatile uint8_t* mmio = (volatile uint8_t*)(uintptr_t)mmio_base;
    uint64_t cap = *nvme_reg64(mmio, 0x00);
    uint32_t mqes = (uint32_t)(cap & 0xFFFFu);
    uint32_t dstrd = (uint32_t)((cap >> 32) & 0x0Fu);

    *nvme_reg32(mmio, 0x14) = 0;
    if (!nvme_wait_csts(mmio, 1u, 0)) {
        g_nvme_last_error = "NVMe: kontroler nie wszedl w stan disabled";
        return false;
    }

    out_dev->mmio = mmio;
    out_dev->doorbell_stride = dstrd;
    out_dev->admin.q_depth = min_u16((uint16_t)(mqes + 1), 16);
    out_dev->admin.sq = (NvmeSubmissionQueueEntry*)alloc_aligned(sizeof(NvmeSubmissionQueueEntry) * out_dev->admin.q_depth, 4096);
    out_dev->admin.cq = (NvmeCompletionQueueEntry*)alloc_aligned(sizeof(NvmeCompletionQueueEntry) * out_dev->admin.q_depth, 4096);
    out_dev->admin.cq_phase = 1;
    out_dev->io.q_depth = min_u16((uint16_t)(mqes + 1), 16);
    out_dev->io.sq = (NvmeSubmissionQueueEntry*)alloc_aligned(sizeof(NvmeSubmissionQueueEntry) * out_dev->io.q_depth, 4096);
    out_dev->io.cq = (NvmeCompletionQueueEntry*)alloc_aligned(sizeof(NvmeCompletionQueueEntry) * out_dev->io.q_depth, 4096);
    out_dev->io.cq_phase = 1;
    if (!out_dev->admin.sq || !out_dev->admin.cq || !out_dev->io.sq || !out_dev->io.cq) {
        g_nvme_last_error = "NVMe: brak pamieci na kolejki";
        return false;
    }

    *nvme_reg32(mmio, 0x24) = ((uint32_t)(out_dev->admin.q_depth - 1) << 16) | (uint32_t)(out_dev->admin.q_depth - 1);
    *nvme_reg64(mmio, 0x28) = (uint64_t)(uintptr_t)out_dev->admin.sq;
    *nvme_reg64(mmio, 0x30) = (uint64_t)(uintptr_t)out_dev->admin.cq;
    *nvme_reg32(mmio, 0x14) = (6u << 20) | (4u << 16) | 1u;
    if (!nvme_wait_csts(mmio, 1u, 1u)) {
        g_nvme_last_error = "NVMe: kontroler nie przeszedl w stan ready";
        return false;
    }

    if (!nvme_create_io_cq(out_dev, 1, out_dev->io.q_depth)) {
        g_nvme_last_error = "NVMe: Create IO CQ nie powiodlo sie";
        return false;
    }
    if (!nvme_create_io_sq(out_dev, 1, out_dev->io.q_depth)) {
        g_nvme_last_error = "NVMe: Create IO SQ nie powiodlo sie";
        return false;
    }

    uint8_t* identify = (uint8_t*)alloc_aligned(4096, 4096);
    if (!identify) {
        g_nvme_last_error = "NVMe: brak pamieci na bufor identify";
        return false;
    }
    if (!nvme_identify_controller(out_dev, identify)) {
        g_nvme_last_error = "NVMe: Identify Controller nie powiodlo sie";
        return false;
    }
    uint32_t nn = *(uint32_t*)(identify + 516);
    if (nn == 0) {
        g_nvme_last_error = "NVMe: kontroler nie raportuje namespace";
        return false;
    }

    mem_zero(identify, 4096);
    if (!nvme_identify_namespace(out_dev, 1, identify)) {
        g_nvme_last_error = "NVMe: Identify Namespace nie powiodlo sie";
        return false;
    }
    uint64_t nsze = *(uint64_t*)(identify + 0);
    if (nsze == 0) {
        g_nvme_last_error = "NVMe: namespace ma rozmiar 0";
        return false;
    }
    uint8_t nlbaf = identify[25];
    uint8_t flbas = identify[26] & 0x0F;
    if (flbas > nlbaf) {
        g_nvme_last_error = "NVMe: niepoprawne LBA format w namespace";
        return false;
    }
    uint8_t lbads = identify[128 + flbas * 4 + 2];
    uint32_t lba_size = 1u << lbads;
    if (lba_size != 512) {
        register_unsupported_controller(bus, slot, func, "NVMe controller", "NVMe: logiczny rozmiar sektora != 512");
        return false;
    }

    out_dev->namespace_id = 1;
    out_dev->lba_size = lba_size;

    copy_cstr(out_disk->transport, "NVMe", sizeof(out_disk->transport));
    out_disk->sector_size = lba_size;
    out_disk->sector_count = nsze;
    out_disk->removable = false;
    return true;
}

static bool disk_read_sector(const DiskInfo* disk, uint64_t lba, void* buffer) {
    uint32_t index = (uint32_t)(disk - g_disks);
    if (index >= g_disk_count) return false;
    if (g_disk_backends[index] == DISK_BACKEND_AHCI) {
        return ahci_read_sector(g_ahci_devices[index].port, g_ahci_devices[index].port_index, lba, buffer);
    }
    if (g_disk_backends[index] == DISK_BACKEND_IDE) {
        return ide_read_sector(g_ide_devices[index].channel, g_ide_devices[index].drive_select, lba, buffer);
    }
    if (g_disk_backends[index] == DISK_BACKEND_NVME) {
        NvmeDiskDevice* nvme = &g_nvme_devices[index];
        if (!nvme->mmio || nvme->lba_size != 512) return false;
        return nvme_read_one_sector(nvme, lba, buffer);
    }
    return false;
}

static const char* mbr_type_name(uint8_t type) {
    switch (type) {
    case 0x01: return "FAT12";
    case 0x04: return "FAT16<32M";
    case 0x06: return "FAT16";
    case 0x07: return "NTFS/exFAT";
    case 0x0B: return "FAT32";
    case 0x0C: return "FAT32 LBA";
    case 0x0E: return "FAT16 LBA";
    case 0x0F: return "Extended";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0x8E: return "Linux LVM";
    case 0xA5: return "BSD";
    case 0xAF: return "Apple HFS";
    case 0xEE: return "GPT protective";
    default: return "Unknown";
    }
}

static void set_partition_name(DiskPartitionInfo* part, const char* name) {
    copy_cstr(part->type_name, name, sizeof(part->type_name));
}

static void detect_mbr_partitions(DiskInfo* disk, const uint8_t* sector0) {
    const MbrPartitionEntry* entries = (const MbrPartitionEntry*)(sector0 + 446);
    disk->partition_style = PARTITION_STYLE_MBR;
    disk->partition_count = 0;

    for (uint32_t i = 0; i < 4 && disk->partition_count < kMaxPartitionsPerDisk; i++) {
        if (entries[i].type == 0 || entries[i].sector_count == 0) continue;
        DiskPartitionInfo* part = &disk->partitions[disk->partition_count++];
        part->present = true;
        part->first_lba = entries[i].first_lba;
        part->sector_count = entries[i].sector_count;
        set_partition_name(part, mbr_type_name(entries[i].type));
        part->filesystem[0] = '\0';
    }
}

static const uint8_t kGuidEfiSystem[16] = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
static const uint8_t kGuidMicrosoftReserved[16] = { 0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D, 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE };
static const uint8_t kGuidMicrosoftBasicData[16] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
static const uint8_t kGuidWindowsRecovery[16] = { 0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x4D, 0x40, 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC };
static const uint8_t kGuidLinuxFilesystem[16] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };

static const char* gpt_type_name(const uint8_t* type_guid) {
    static const uint8_t zero_guid[16] = {};
    if (guid_equals(type_guid, zero_guid)) return nullptr;
    if (guid_equals(type_guid, kGuidEfiSystem)) return "EFI System";
    if (guid_equals(type_guid, kGuidMicrosoftReserved)) return "Microsoft Reserved";
    if (guid_equals(type_guid, kGuidMicrosoftBasicData)) return "Basic Data";
    if (guid_equals(type_guid, kGuidWindowsRecovery)) return "Windows Recovery";
    if (guid_equals(type_guid, kGuidLinuxFilesystem)) return "Linux Filesystem";
    return "GPT Partition";
}

static void detect_gpt_partitions(DiskInfo* disk) {
    if (!disk_read_sector(disk, 1, g_sector_buffer)) return;
    const GptHeader* header = (const GptHeader*)g_sector_buffer;
    if (header->signature != 0x5452415020494645ull) return;

    disk->partition_style = PARTITION_STYLE_GPT;
    disk->partition_count = 0;

    uint32_t entry_size = header->partition_entry_size;
    if (entry_size < sizeof(GptPartitionEntry)) return;

    uint32_t max_entries = min_u32(header->partition_entry_count, 32);
    uint64_t bytes_needed = (uint64_t)entry_size * max_entries;
    uint32_t sectors_to_read = (uint32_t)((bytes_needed + kSectorSize - 1) / kSectorSize);
    sectors_to_read = min_u32(sectors_to_read, 8);

    for (uint32_t i = 0; i < sectors_to_read; i++) {
        if (!disk_read_sector(disk, header->partition_entries_lba + i, g_gpt_entry_buffer + (i * kSectorSize))) {
            return;
        }
    }

    for (uint32_t i = 0; i < max_entries && disk->partition_count < kMaxPartitionsPerDisk; i++) {
        const uint8_t* raw = g_gpt_entry_buffer + (uint64_t)i * entry_size;
        const GptPartitionEntry* entry = (const GptPartitionEntry*)raw;
        const char* type = gpt_type_name(entry->type_guid);
        if (!type) continue;
        if (entry->last_lba < entry->first_lba) continue;

        DiskPartitionInfo* part = &disk->partitions[disk->partition_count++];
        part->present = true;
        part->first_lba = entry->first_lba;
        part->sector_count = entry->last_lba - entry->first_lba + 1;
        set_partition_name(part, type);
        part->filesystem[0] = '\0';
    }
}

static void detect_partition_filesystems(DiskInfo* disk) {
    for (uint32_t i = 0; i < disk->partition_count; i++) {
        DiskPartitionInfo* part = &disk->partitions[i];
        part->filesystem[0] = '\0';
        if (!part->present) continue;
        if (!disk_read_sector(disk, part->first_lba, g_sector_buffer)) continue;
        const char* fs_name = detect_fat_boot_sector_kind(g_sector_buffer);
        if (fs_name) set_partition_filesystem(part, fs_name);
    }
}

static void detect_partition_table(DiskInfo* disk) {
    for (uint32_t i = 0; i < kMaxPartitionsPerDisk; i++) {
        disk->partitions[i].present = false;
    }
    disk->partition_count = 0;
    disk->partition_style = PARTITION_STYLE_NONE;

    if (!disk_read_sector(disk, 0, g_sector_buffer)) return;
    if (g_sector_buffer[510] != 0x55 || g_sector_buffer[511] != 0xAA) return;

    const MbrPartitionEntry* entries = (const MbrPartitionEntry*)(g_sector_buffer + 446);
    bool protective_gpt = false;
    for (uint32_t i = 0; i < 4; i++) {
        if (entries[i].type == 0xEE) {
            protective_gpt = true;
            break;
        }
    }

    if (protective_gpt) {
        detect_gpt_partitions(disk);
        if (disk->partition_style == PARTITION_STYLE_GPT) {
            detect_partition_filesystems(disk);
            return;
        }
    }

    detect_mbr_partitions(disk, g_sector_buffer);
    detect_partition_filesystems(disk);
}

static void fill_disk_name(DiskInfo* disk, uint32_t index) {
    disk->name[0] = 'd';
    disk->name[1] = 'i';
    disk->name[2] = 's';
    disk->name[3] = 'k';
    disk->name[4] = (char)('0' + (index % 10));
    disk->name[5] = '\0';
}

static void register_ahci_disk(HbaPort* port, uint32_t port_index, const uint8_t* identify_data) {
    if (g_disk_count >= kMaxDisks) return;

    uint32_t disk_index = g_disk_count;
    DiskInfo* disk = &g_disks[disk_index];
    clear_disk_info(disk);
    fill_disk_name(disk, disk_index);
    copy_cstr(disk->transport, "AHCI/SATA", sizeof(disk->transport));
    disk->sector_size = kSectorSize;
    disk->removable = ((identify_data[0] & 0x80) != 0);
    extract_sector_count(identify_data, &disk->sector_count);

    g_disk_backends[disk_index] = DISK_BACKEND_AHCI;
    g_ahci_devices[disk_index].port = port;
    g_ahci_devices[disk_index].port_index = (uint8_t)port_index;
    g_disk_count = disk_index + 1;

    detect_partition_table(disk);
    disk->present = true;
}

static void register_ide_disk(const IdeChannel& channel, uint8_t drive_select, const uint8_t* identify_data) {
    if (g_disk_count >= kMaxDisks) return;

    uint32_t disk_index = g_disk_count;
    DiskInfo* disk = &g_disks[disk_index];
    clear_disk_info(disk);
    fill_disk_name(disk, disk_index);
    copy_cstr(disk->transport, "IDE/ATA", sizeof(disk->transport));
    disk->sector_size = kSectorSize;
    disk->removable = ((identify_data[0] & 0x80) != 0);
    if (!extract_sector_count(identify_data, &disk->sector_count)) return;

    g_disk_backends[disk_index] = DISK_BACKEND_IDE;
    g_ide_devices[disk_index].channel = channel;
    g_ide_devices[disk_index].drive_select = drive_select;
    g_disk_count = disk_index + 1;

    detect_partition_table(disk);
    disk->present = true;
}

static void detect_nvme_controller(uint8_t bus, uint8_t slot, uint8_t func, uint8_t prog_if) {
    if (g_disk_count >= kMaxDisks) return;
    g_nvme_last_error = "NVMe: inicjalizacja nie powiodla sie";
    if (prog_if != kPciProgIfNvme) {
        register_unsupported_controller(bus, slot, func, "NVMe controller", "NVMe: nieoczekiwany programming interface");
        return;
    }

    uint16_t command = pci_read16(bus, slot, func, 0x04);
    command |= (1u << 1) | (1u << 2);
    pci_write16(bus, slot, func, 0x04, command);

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if ((bar0 & 1u) != 0) {
        register_unsupported_controller(bus, slot, func, "NVMe controller", "NVMe: BAR0 jest I/O zamiast MMIO");
        return;
    }

    uint64_t mmio_base = (uint64_t)(bar0 & ~0x0Fu);
    if ((bar0 & 0x6u) == 0x4u) {
        uint32_t bar1 = pci_read32(bus, slot, func, 0x14);
        mmio_base |= (uint64_t)bar1 << 32;
    }
    if (mmio_base == 0) {
        register_unsupported_controller(bus, slot, func, "NVMe controller", "NVMe: brak poprawnego MMIO BAR");
        return;
    }

    uint32_t disk_index = g_disk_count;
    DiskInfo* disk = &g_disks[disk_index];
    clear_disk_info(disk);
    fill_disk_name(disk, disk_index);

    NvmeDiskDevice* dev = &g_nvme_devices[disk_index];
    mem_zero(dev, sizeof(NvmeDiskDevice));
    if (!nvme_prepare_controller(bus, slot, func, mmio_base, dev, disk)) {
        register_unsupported_controller(bus, slot, func, "NVMe controller", g_nvme_last_error);
        return;
    }

    g_disk_backends[disk_index] = DISK_BACKEND_NVME;
    g_disk_count = disk_index + 1;
    detect_partition_table(disk);
    disk->present = true;
}

static void detect_ide_channel(const IdeChannel& channel) {
    for (uint8_t drive = 0; drive < 2 && g_disk_count < kMaxDisks; drive++) {
        uint8_t identify_data[kSectorSize];
        if (!ide_identify(channel, drive, identify_data)) continue;
        register_ide_disk(channel, drive, identify_data);
    }
}

static void detect_ide_controller(uint8_t bus, uint8_t slot, uint8_t func, uint8_t prog_if) {
    const bool primary_native = (prog_if & 0x01) != 0;
    const bool secondary_native = (prog_if & 0x04) != 0;
    if (primary_native || secondary_native) {
        register_unsupported_controller(bus, slot, func, "IDE controller", "native mode IDE jeszcze nieobslugiwane");
        return;
    }

    uint16_t command = pci_read16(bus, slot, func, 0x04);
    command |= 1u;
    pci_write16(bus, slot, func, 0x04, command);

    IdeChannel primary = { 0x1F0, 0x3F6 };
    IdeChannel secondary = { 0x170, 0x376 };
    detect_ide_channel(primary);
    detect_ide_channel(secondary);
}

static void detect_ahci_controller(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t command = pci_read16(bus, slot, func, 0x04);
    command |= (1u << 1) | (1u << 2);
    pci_write16(bus, slot, func, 0x04, command);

    uint32_t abar = pci_read32(bus, slot, func, 0x24) & ~0x0Fu;
    if (abar == 0) return;

    HbaMem* hba = (HbaMem*)(uintptr_t)abar;
    hba->ghc |= (1u << 31);
    if (hba->cap2 & 1u) {
        hba->bohc |= (1u << 1);
        for (uint32_t spin = 0; spin < 1000000; spin++) {
            if ((hba->bohc & 1u) == 0) break;
        }
    }
    uint32_t port_mask = hba->pi;

    for (uint32_t port_index = 0; port_index < 32; port_index++) {
        if ((port_mask & (1u << port_index)) == 0) continue;
        HbaPort* port = &hba->ports[port_index];
        uint32_t ssts = port->ssts;
        uint32_t det = ssts & 0x0F;
        uint32_t ipm = (ssts >> 8) & 0x0F;
        if (det != kAhciPortDetPresent || ipm != kAhciPortIpmActive) continue;
        if (port->sig != kSataSigAta) continue;

        ahci_rebase_port(port, port_index);
        uint8_t identify_data[kSectorSize];
        if (!ahci_identify(port, port_index, identify_data)) continue;
        register_ahci_disk(port, port_index, identify_data);
    }
}

void storage_init() {
    g_disk_count = 0;
    g_unsupported_count = 0;
    for (uint32_t i = 0; i < kMaxDisks; i++) {
        clear_disk_info(&g_disks[i]);
        g_disk_backends[i] = DISK_BACKEND_NONE;
        g_ahci_devices[i].port = nullptr;
        g_ahci_devices[i].port_index = 0;
        g_ide_devices[i].channel = {};
        g_ide_devices[i].drive_select = 0;
        mem_zero(&g_nvme_devices[i], sizeof(NvmeDiskDevice));
    }
    for (uint32_t i = 0; i < kMaxUnsupportedControllers; i++) {
        clear_unsupported_info(&g_unsupported[i]);
    }

    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, (uint8_t)slot, 0, 0x0E);
            uint32_t function_count = (header_type & 0x80) ? 8 : 1;
            for (uint32_t func = 0; func < function_count; func++) {
                vendor = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                if (vendor == 0xFFFF) continue;

                uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0B);
                uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0A);
                uint8_t prog_if = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x09);
                if (class_code == kPciClassMassStorage && subclass == kPciSubclassAhci && prog_if == kPciProgIfAhci) {
                    detect_ahci_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                    if (g_disk_count >= kMaxDisks) return;
                }
                else if (class_code == kPciClassMassStorage && subclass == kPciSubclassIde) {
                    detect_ide_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func, prog_if);
                    if (g_disk_count >= kMaxDisks) return;
                }
                else if (class_code == kPciClassMassStorage && subclass == kPciSubclassNvme) {
                    detect_nvme_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func, prog_if);
                    if (g_disk_count >= kMaxDisks) return;
                }
                else if (class_code == kPciClassMassStorage) {
                    register_unsupported_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                        mass_storage_name(subclass, prog_if), "brak sterownika storage");
                }
                else if (class_code == kPciClassSerialBus && subclass == kPciSubclassUsb) {
                    register_unsupported_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                        usb_controller_name(prog_if), "brak stosu USB Mass Storage");
                }
            }
        }
    }
}

void storage_register_boot_volume(const BootVolumeHandoff* handoff) {
    if (!handoff || handoff->present == 0 || g_disk_count >= kMaxDisks) return;

    DiskInfo* disk = &g_disks[g_disk_count];
    clear_disk_info(disk);
    copy_cstr(disk->name, "boot", sizeof(disk->name));
    copy_cstr(disk->transport, "UEFI Boot", sizeof(disk->transport));
    disk->removable = handoff->removable != 0;
    disk->sector_size = handoff->block_size ? handoff->block_size : 512;
    disk->sector_count = handoff->block_count;
    disk->partition_style = handoff->logical_partition ? PARTITION_STYLE_NONE : PARTITION_STYLE_NONE;

    const char* fs_name = nullptr;
    switch (handoff->fs_kind) {
    case BOOT_FS_FAT12: fs_name = "FAT12"; break;
    case BOOT_FS_FAT16: fs_name = "FAT16"; break;
    case BOOT_FS_FAT32: fs_name = "FAT32"; break;
    default: break;
    }
    if (!fs_name && handoff->sector0_valid) {
        fs_name = detect_fat_boot_sector_kind(handoff->sector0);
    }
    if (fs_name) {
        set_disk_filesystem(disk, fs_name);
    }

    disk->present = true;
    g_disk_backends[g_disk_count] = DISK_BACKEND_NONE;
    g_disk_count++;
}

uint32_t storage_disk_count() {
    return g_disk_count;
}

const DiskInfo* storage_get_disk(uint32_t index) {
    if (index >= g_disk_count) return nullptr;
    return &g_disks[index];
}

bool storage_read_disk_sector(uint32_t disk_index, uint64_t lba, void* buffer) {
    if (disk_index >= g_disk_count) return false;
    const DiskInfo* disk = &g_disks[disk_index];
    if (!disk->present) return false;
    return disk_read_sector(disk, lba, buffer);
}

bool storage_read_partition_sector(uint32_t disk_index, uint32_t partition_index, uint64_t rel_lba, void* buffer) {
    if (disk_index >= g_disk_count) return false;
    const DiskInfo* disk = &g_disks[disk_index];
    if (!disk->present) return false;
    if (partition_index >= disk->partition_count) return false;
    const DiskPartitionInfo* part = &disk->partitions[partition_index];
    if (!part->present) return false;
    return disk_read_sector(disk, part->first_lba + rel_lba, buffer);
}

const char* storage_partition_style_name(PartitionStyle style) {
    switch (style) {
    case PARTITION_STYLE_MBR: return "MBR";
    case PARTITION_STYLE_GPT: return "GPT";
    default: return "none";
    }
}

const char* storage_boot_fs_name(uint32_t fs_kind) {
    switch (fs_kind) {
    case BOOT_FS_FAT12: return "FAT12";
    case BOOT_FS_FAT16: return "FAT16";
    case BOOT_FS_FAT32: return "FAT32";
    default: return "unknown";
    }
}

uint32_t storage_unsupported_count() {
    return g_unsupported_count;
}

const UnsupportedControllerInfo* storage_get_unsupported(uint32_t index) {
    if (index >= g_unsupported_count) return nullptr;
    return &g_unsupported[index];
}
