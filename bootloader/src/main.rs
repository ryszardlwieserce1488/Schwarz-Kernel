#![no_std]
#![no_main]

use uefi::prelude::*;
use uefi::proto::loaded_image::LoadedImage;
use uefi::proto::media::block::BlockIO;
use uefi::proto::media::fs::SimpleFileSystem;
use uefi::proto::media::file::{
    File, FileAttribute, FileInfo, FileMode, FileType,
};
use uefi::proto::console::gop::GraphicsOutput;
use uefi::table::boot::{AllocateType, MemoryType};
use uefi::table::cfg;

#[repr(C)]
pub struct MemoryRegion {
    pub base: u64,
    pub length: u64,
    pub kind: u32,   // 1 = używalne, 0 = zarezerwowane
    pub pad: u32,
}

#[repr(C)]
pub struct BootInfo {
    pub framebuffer_addr: u64,
    pub framebuffer_size: u64,
    pub horizontal_resolution: u32,
    pub vertical_resolution: u32,
    pub memory_map_addr: u64,
    pub memory_map_count: u64,
    pub rsdp_addr: u64,  // NOWE
    pub boot_volume: BootVolumeHandoff,
}

#[repr(C)]
pub struct BootVolumeHandoff {
    pub present: u32,
    pub removable: u32,
    pub logical_partition: u32,
    pub fs_kind: u32,
    pub block_size: u32,
    pub sector0_valid: u32,
    pub block_count: u64,
    pub sector0: [u8; 512],
}

fn empty_boot_volume() -> BootVolumeHandoff {
    BootVolumeHandoff {
        present: 0,
        removable: 0,
        logical_partition: 0,
        fs_kind: 0,
        block_size: 0,
        sector0_valid: 0,
        block_count: 0,
        sector0: [0u8; 512],
    }
}

fn detect_fat_kind(sector0: &[u8; 512]) -> u32 {
    if sector0[510] != 0x55 || sector0[511] != 0xAA {
        return 0;
    }

    let bytes_per_sector = u16::from_le_bytes([sector0[11], sector0[12]]) as u32;
    let sectors_per_cluster = sector0[13] as u32;
    let reserved_sector_count = u16::from_le_bytes([sector0[14], sector0[15]]) as u32;
    let num_fats = sector0[16] as u32;
    let root_entry_count = u16::from_le_bytes([sector0[17], sector0[18]]) as u32;
    let total_sectors_16 = u16::from_le_bytes([sector0[19], sector0[20]]) as u32;
    let fat_size_16 = u16::from_le_bytes([sector0[22], sector0[23]]) as u32;
    let total_sectors_32 = u32::from_le_bytes([sector0[32], sector0[33], sector0[34], sector0[35]]);
    let fat_size_32 = u32::from_le_bytes([sector0[36], sector0[37], sector0[38], sector0[39]]);

    if !matches!(bytes_per_sector, 512 | 1024 | 2048 | 4096) {
        return 0;
    }
    if sectors_per_cluster == 0 || reserved_sector_count == 0 || num_fats == 0 {
        return 0;
    }

    let total_sectors = if total_sectors_16 != 0 { total_sectors_16 } else { total_sectors_32 };
    let fat_size = if fat_size_16 != 0 { fat_size_16 } else { fat_size_32 };
    if total_sectors == 0 || fat_size == 0 {
        return 0;
    }

    let root_dir_sectors = ((root_entry_count * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
    let overhead = reserved_sector_count + (num_fats * fat_size) + root_dir_sectors;
    if total_sectors <= overhead {
        return 0;
    }

    let data_sectors = total_sectors - overhead;
    let cluster_count = data_sectors / sectors_per_cluster;
    if cluster_count < 4085 {
        1
    } else if cluster_count < 65525 {
        2
    } else {
        3
    }
}

#[entry]
fn main(handle: Handle, mut system_table: SystemTable<Boot>) -> Status {
    uefi_services::init(&mut system_table).unwrap();

    let (kernel_addr, fb_addr, fb_size, h_res, v_res, boot_info_addr, regions_addr, rsdp_addr, boot_volume) = {
        let bt = system_table.boot_services();

        let loaded_image = bt
            .open_protocol_exclusive::<LoadedImage>(handle)
            .unwrap();
        let image_device = loaded_image.device();

        // === Załaduj kernel ===
        let mut sfs = bt
            .open_protocol_exclusive::<SimpleFileSystem>(image_device)
            .unwrap();
        let mut root = sfs.open_volume().unwrap();

        let boot_volume = {
            let mut info = empty_boot_volume();
            if let Ok(block_io) = bt.open_protocol_exclusive::<BlockIO>(image_device) {
                let media = block_io.media();
                info.present = if media.is_media_present() { 1 } else { 0 };
                info.removable = if media.is_removable_media() { 1 } else { 0 };
                info.logical_partition = if media.is_logical_partition() { 1 } else { 0 };
                info.block_size = media.block_size();
                info.block_count = media.last_block().saturating_add(1);

                let block_size = media.block_size() as usize;
                if (512..=4096).contains(&block_size) {
                    let mut block_buf = [0u8; 4096];
                    if block_io
                        .read_blocks(media.media_id(), 0, &mut block_buf[..block_size])
                        .is_ok()
                    {
                        info.sector0[..512].copy_from_slice(&block_buf[..512]);
                        info.sector0_valid = 1;
                        info.fs_kind = detect_fat_kind(&info.sector0);
                    }
                }
            }
            info
        };

        let file_handle = match root.open(
            uefi::cstr16!("system\\kernel.bin"),
            FileMode::Read,
            FileAttribute::empty(),
        ) {
            Ok(f) => f,
            Err(_) => {
                uefi_services::println!("Blad: Nie znaleziono X:\\system\\kernel.bin");
                return Status::NOT_FOUND;
            }
        };

        let mut kernel_file = match file_handle.into_type().unwrap() {
            FileType::Regular(f) => f,
            _ => {
                uefi_services::println!("kernel.bin nie jest plikiem regularnym");
                return Status::LOAD_ERROR;
            }
        };

        let mut info_buf = [0u8; 256];
        let info = kernel_file
            .get_info::<FileInfo>(&mut info_buf)
            .unwrap();

        let file_size = info.file_size() as usize;

        // dodajemy zapas stron na .bss i inne dane globalne
        let pages = (file_size + 4095) / 4096 + 512;  // 512 stron = 2MB zapasu na BSS
       
        let kernel_addr = match bt.allocate_pages(
            AllocateType::Address(0x100000u64),
            MemoryType::LOADER_DATA,
            pages,
        ) {
            Ok(addr) => {
                uefi_services::println!("Udalo sie zaalokowac pod {:#x}", addr);
                addr
            }
            Err(e) => {
                uefi_services::println!("Nie udalo sie pod 0x100000: {:?}", e);
                bt.allocate_pages(
                    AllocateType::AnyPages,
                    MemoryType::LOADER_DATA,
                    pages,
                ).unwrap()
            }
        };
        uefi_services::println!("file_size: {} bajtow", file_size);
        uefi_services::println!("pages: {}", pages);
        uefi_services::println!("zeruje od: {:#x} do: {:#x}", 
            kernel_addr + file_size as u64,
            kernel_addr + pages as u64 * 4096);
        
        uefi_services::println!("Jadro zaladowane pod adresem {:#x}", kernel_addr);

        let buffer = unsafe {
            core::slice::from_raw_parts_mut(kernel_addr as *mut u8, file_size)
        };

        kernel_file.read(buffer).unwrap();
        kernel_file.close();


        // ==============================
        // WYZERUJ RESZTĘ PAMIĘCI (.bss)
        // ==============================

        let total_size = pages * 4096;

        unsafe {
            core::ptr::write_bytes(
                (kernel_addr as *mut u8).add(file_size),
                0,
                total_size - file_size,
            );
        }

        // === GOP ===
        let gop_handle = bt
            .get_handle_for_protocol::<GraphicsOutput>()
            .unwrap();
        let mut gop = bt
            .open_protocol_exclusive::<GraphicsOutput>(gop_handle)
            .unwrap();

        let mode_info = gop.current_mode_info();
        let mut fb = gop.frame_buffer();

        uefi_services::println!("=== DIAGNOSTYKA GOP ===");
        uefi_services::println!("Adres framebuffera: {:#x}", fb.as_mut_ptr() as u64);
        uefi_services::println!("Rozmiar framebuffera: {} bajtow", fb.size());
        uefi_services::println!("Rozdzielczosc: {}x{}",
            mode_info.resolution().0,
            mode_info.resolution().1);
        uefi_services::println!("Format pikseli: {:?}", mode_info.pixel_format());
        uefi_services::println!("Stride: {}", mode_info.stride());

        let fb_addr   = fb.as_mut_ptr() as u64;
        let fb_size   = fb.size() as u64;
        let h_res     = mode_info.resolution().0 as u32;
        let v_res     = mode_info.resolution().1 as u32;
        drop(gop);

        // === Zaalokuj bufory na mapę i BootInfo ===
        // 256 regionów * 16 bajtów = 4096 = 1 strona
        let regions_addr = bt.allocate_pages(
            AllocateType::AnyPages,
            MemoryType::LOADER_DATA,
            1,
        ).unwrap();

        let boot_info_addr = bt.allocate_pages(
            AllocateType::AnyPages,
            MemoryType::LOADER_DATA,
            1,
        ).unwrap();
        let rsdp_addr = system_table.config_table()
            .iter()
            .find(|e| e.guid == cfg::ACPI2_GUID || e.guid == cfg::ACPI_GUID)
            .map(|e| e.address as u64)
            .unwrap_or(0);
        uefi_services::println!("rsdp value: {:#x}", rsdp_addr);
uefi_services::println!("bootinfo size: {}", core::mem::size_of::<BootInfo>());
        bt.stall(3_000_000);

        (kernel_addr, fb_addr, fb_size, h_res, v_res, boot_info_addr, regions_addr, rsdp_addr, boot_volume)
    };

    // === exit_boot_services — od tego momentu nie ma UEFI ===
    let (_rt, mmap) = system_table.exit_boot_services();

    // === Wypełnij mapę pamięci ===
    let regions = unsafe {
        core::slice::from_raw_parts_mut(
            regions_addr as *mut MemoryRegion,
            256,
        )
    };

    let mut count = 0u64;
    for entry in mmap.entries() {
        if count >= 256 { break; }

        let kind = match entry.ty {
            MemoryType::CONVENTIONAL       => 1u32,
            MemoryType::BOOT_SERVICES_CODE => 1u32,
            MemoryType::BOOT_SERVICES_DATA => 1u32,
            _                              => 0u32,
        };

        regions[count as usize] = MemoryRegion {
            base:   entry.phys_start,
            length: entry.page_count * 4096,
            kind,
            pad: 0,
        };
        count += 1;
    }

    // === Wypełnij BootInfo ===
    let boot_info = unsafe { &mut *(boot_info_addr as *mut BootInfo) };
    boot_info.framebuffer_addr       = fb_addr;
    boot_info.framebuffer_size       = fb_size;
    boot_info.horizontal_resolution  = h_res;
    boot_info.vertical_resolution    = v_res;
    boot_info.memory_map_addr        = regions_addr;
    boot_info.memory_map_count       = count;
    boot_info.rsdp_addr = rsdp_addr;
    boot_info.boot_volume = boot_volume;

    // === Skocz do jądra ===
    unsafe {
        let entry: extern "C" fn(*const BootInfo) -> ! =
            core::mem::transmute(kernel_addr);
        entry(boot_info as *const BootInfo);
    }
}
