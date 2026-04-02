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
