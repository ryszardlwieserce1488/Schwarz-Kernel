//usb.h
#pragma once
#include "types.h"

static constexpr uint32_t USB_MAX_XHCI_CONTROLLERS = 4;
static constexpr uint32_t USB_MAX_PORTS = 32;
static constexpr uint32_t USB_MAX_DEVICES = 32;
static constexpr uint32_t USB_MAX_SLOTS = 64;

struct UsbPortInfo {
    bool connected;
    bool enabled;
    bool powered;
    bool enable_slot_ok;
    bool reset_ok;
    uint8_t speed_id;
    uint8_t slot_id;
};

struct UsbDeviceInfo {
    bool present;
    bool addressed;
    bool descriptor_ok;
    bool is_mass_storage;
    uint8_t port_index;
    uint8_t slot_id;
    uint8_t speed_id;
    uint8_t usb_class;
    uint8_t usb_subclass;
    uint8_t usb_protocol;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t interface_count;
    uint8_t configuration_value;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
    uint8_t address_completion_code;
    uint8_t descriptor_completion_code;
    uint8_t config_completion_code;
    uint8_t config_descriptor_count;
    uint8_t config_first_types[8];
    uint8_t config_header_len;
    uint8_t config_header_type;
    uint8_t config_header_bytes[8];
    uint16_t device_bytes_transferred;
    uint16_t config_bytes_transferred;
    uint16_t config_total_length;
    uint16_t vendor_id;
    uint16_t product_id;
    bool bulk_configured;
    bool     disk_read_ok;
    uint32_t disk_last_lba;
    uint32_t disk_block_size;
    uint8_t  disk_sector0[512];
    bool     bot_inquiry_ok;
    bool     bot_capacity_ok;
    uint8_t  bot_last_completion_code;
    uint32_t bot_cbw_ok;   // 1=cbw sent, 0=fail
    uint32_t bot_data_ok;  // 1=data phase ok
    uint32_t bot_csw_ok;   // 1=csw received ok
    uint8_t  bot_csw_status;
    uint32_t bot_csw_sig;
    uint8_t bulk_cfg_completion_code;
    uint32_t bulk_cfg_ep_out_ctx;
    uint32_t bulk_cfg_ep_in_ctx;
    uint32_t bulk_cfg_icc1;
    uint32_t bulk_cfg_entries;
    uint8_t  bot_cbw_phase;  
    uint8_t bot_bulk_out_cc;
    uint8_t bot_bulk_in_cc;
    uint32_t dbg_event_type;
    uint32_t dbg_event_ptr_lo;
    uint32_t dbg_event_ptr_hi;
    uint32_t dbg_target_lo;
    uint32_t dbg_target_hi;
    uint8_t  dbg_event_cc;
    uint32_t dbg_ring_lo;
    uint32_t dbg_in_ring_lo;
	uint32_t dbg_in_ring_hi;
    uint32_t dbg_ring_hi;
    uint32_t dbg_idx;
    uint32_t dbg_in_target_lo;
    uint32_t dbg_in_target_hi;
    uint32_t dbg_in_event_ptr_lo;
    uint32_t dbg_in_event_ptr_hi;
    uint32_t dbg_in_event_type;
    uint32_t dbg_in_idx;
    uint32_t dbg_in_trb_ctrl;
    uint32_t dbg_in_trb_stat;
    uint32_t dbg_in_trb_p0;
    uint32_t dbg_cfg_in_ring_lo;
};

struct UsbXhciControllerInfo {
    bool present;
    bool initialized;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t max_ports;
    uint8_t max_slots;
    uint64_t mmio_base;
    UsbPortInfo ports_before_handoff[USB_MAX_PORTS];
    UsbPortInfo ports_after_handoff[USB_MAX_PORTS];
    uint32_t device_count;
    UsbDeviceInfo devices[USB_MAX_DEVICES];
};

void usb_init();
uint32_t usb_xhci_count();
const UsbXhciControllerInfo* usb_get_xhci(uint32_t index);
const char* usb_speed_name(uint8_t speed_id);
bool usb_read_disk_sector(uint32_t device_index, uint64_t lba, void* buffer);
void usb_register_storage_disks();