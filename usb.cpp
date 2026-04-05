//usb.cpp
#include "usb.h"
#include "memory.h"
#include "storage.h"
extern "C" uint32_t inl(uint16_t port);
extern "C" void outl(uint16_t port, uint32_t value);

static constexpr uint16_t kPciConfigAddress = 0xCF8;
static constexpr uint16_t kPciConfigData = 0xCFC;
static constexpr uint8_t kPciClassSerialBus = 0x0C;
static constexpr uint8_t kPciSubclassUsb = 0x03;
static constexpr uint8_t kPciProgIfXhci = 0x30;
static constexpr uint32_t kUsbcmdRunStop = 1u << 0;
static constexpr uint32_t kUsbcmdHcReset = 1u << 1;
static constexpr uint32_t kUsbcmdInterrupterEnable = 1u << 2;
static constexpr uint32_t kUsbstsHcHalted = 1u << 0;
static constexpr uint32_t kUsbstsControllerNotReady = 1u << 11;
static constexpr uint32_t kPortscCcs = 1u << 0;
static constexpr uint32_t kPortscPed = 1u << 1;
static constexpr uint32_t kPortscPortPower = 1u << 9;
static constexpr uint32_t kPortscPortReset = 1u << 4;
static constexpr uint32_t kPortscPrc = 1u << 21;
static constexpr uint32_t kPortscCsc = 1u << 17;
static constexpr uint32_t kPortscWrc = 1u << 19;
static constexpr uint32_t kPortscPec = 1u << 18;
static constexpr uint32_t kPortscOcc = 1u << 20;
static constexpr uint32_t kPortscPlc = 1u << 22;
static constexpr uint32_t kPortscCec = 1u << 23;
static constexpr uint32_t kPortscCas = 1u << 24;
static constexpr uint32_t kPortscWce = 1u << 25;
static constexpr uint32_t kPortscWde = 1u << 26;
static constexpr uint32_t kPortscWoe = 1u << 27;
static constexpr uint32_t kPortscWritePreserveMask = kPortscPortPower | kPortscWce | kPortscWde | kPortscWoe;
static constexpr uint32_t kConfigMaxSlotsMask = 0xFF;
static constexpr uint32_t kTrbTypeEnableSlotCmd = 9;
static constexpr uint32_t kTrbTypeAddressDeviceCmd = 11;
static constexpr uint32_t kTrbTypeCommandCompletion = 33;
static constexpr uint32_t kTrbTypeTransferEvent = 32;
static constexpr uint32_t kTrbTypePortStatusChange = 34;
static constexpr uint32_t kTrbTypeSetupStage = 2;
static constexpr uint32_t kTrbTypeDataStage = 3;
static constexpr uint32_t kTrbTypeStatusStage = 4;
static constexpr uint32_t kTrbCycle = 1u << 0;
static constexpr uint32_t kTrbEnt = 1u << 1;
static constexpr uint32_t kTrbToggleCycle = 1u << 1;
static constexpr uint32_t kTrbIoc = 1u << 5;
static constexpr uint32_t kTrbIdt = 1u << 6;
static constexpr uint32_t kTrbTypeShift = 10;
static constexpr uint32_t kMaxTrbs = 64;
static constexpr uint32_t kEventRingSegmentCount = 1;
static constexpr uint32_t kUsbMaxScratchpads = 32;
static constexpr uint32_t kUsbDescriptorTypeDevice = 1;
static constexpr uint32_t kUsbDescriptorTypeConfiguration = 2;
static constexpr uint32_t kUsbDescriptorTypeInterface = 4;
static constexpr uint8_t kUsbRequestGetDescriptor = 6;
static constexpr uint8_t kUsbRequestTypeDeviceToHost = 0x80;
static constexpr uint8_t kUsbRequestTypeStandard = 0x00;
static constexpr uint8_t kUsbRequestRecipientDevice = 0x00;
static constexpr uint32_t kUsbConfigDescriptorLength = 9;
static constexpr uint32_t kUsbInterfaceDescriptorLength = 9;


struct XhciCapabilityRegs {
    volatile uint8_t cap_length;
    volatile uint8_t reserved;
    volatile uint16_t hci_version;
    volatile uint32_t hcsparams1;
    volatile uint32_t hcsparams2;
    volatile uint32_t hcsparams3;
    volatile uint32_t hccparams1;
    volatile uint32_t dboff;
    volatile uint32_t rtsoff;
    volatile uint32_t hccparams2;
};

struct XhciOperationalRegs {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t pagesize;
    volatile uint32_t reserved0[2];
    volatile uint32_t dnctrl;
    volatile uint64_t crcr;
    volatile uint32_t reserved1[4];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
};

struct XhciInterrupterRegs {
    volatile uint32_t iman;
    volatile uint32_t imod;
    volatile uint32_t erstsz;
    volatile uint32_t reserved;
    volatile uint64_t erstba;
    volatile uint64_t erdp;
};

struct XhciTrb {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
};

struct XhciErstEntry {
    uint64_t ring_segment_base;
    uint16_t ring_segment_size;
    uint16_t reserved0;
    uint32_t reserved1;
};

struct XhciRuntimeState {
    bool resources_ready;
    bool command_ring_running;
    uint8_t command_cycle;
    uint8_t event_cycle;
    uint8_t reserved0;
    uint8_t reserved1;
    uint32_t scratchpad_count;
    uint32_t command_enqueue;
    uint32_t event_dequeue;
    uint64_t* dcbaa;
    uint64_t* scratchpad_array;
    uint8_t* scratchpad_buffers[kUsbMaxScratchpads];
    XhciTrb* command_ring;
    XhciTrb* event_ring;
    XhciErstEntry* erst;
    uint8_t context_size;
    uint8_t reserved2[3];
    uint8_t* input_contexts[USB_MAX_SLOTS];
    uint8_t* device_contexts[USB_MAX_SLOTS];
    XhciTrb* transfer_rings[USB_MAX_SLOTS];
    uint8_t transfer_cycles[USB_MAX_SLOTS];
    uint16_t transfer_enqueue[USB_MAX_SLOTS];
    // Bulk transfer rings: [slot_index][0=OUT, 1=IN]
    XhciTrb* bulk_rings[USB_MAX_SLOTS][2];
    uint8_t   bulk_cycles[USB_MAX_SLOTS][2];
    uint16_t  bulk_enqueue[USB_MAX_SLOTS][2];
};
static constexpr uint32_t kTrbTypeConfigureEndpointCmd = 12;
static constexpr uint32_t kUsbEndpointTypeBulk = 2;
struct UsbSetupPacket {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

struct UsbDeviceDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

static UsbXhciControllerInfo g_xhci[USB_MAX_XHCI_CONTROLLERS];
static XhciRuntimeState g_xhci_runtime[USB_MAX_XHCI_CONTROLLERS];
static uint32_t g_xhci_count = 0;

static void mem_zero(void* dst, uint64_t size) {
    uint8_t* ptr = (uint8_t*)dst;
    while (size--) *ptr++ = 0;
}

static void mem_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (size--) *d++ = *s++;
}

static bool xhci_poll_event(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, XhciTrb* out_trb);
static void xhci_ring_doorbell(volatile XhciCapabilityRegs* cap, uint32_t index, uint32_t value);
static bool xhci_wait_command_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, uint8_t* out_slot_id);
static bool xhci_wait_command_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, uint8_t* out_slot_id, XhciTrb* out_event);
static bool xhci_wait_transfer_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, XhciTrb* target_trb);
static bool xhci_wait_transfer_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, XhciTrb* target_trb, XhciTrb* out_event);

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

static volatile XhciOperationalRegs* xhci_op_regs(volatile XhciCapabilityRegs* cap) {
    return (volatile XhciOperationalRegs*)((uintptr_t)cap + cap->cap_length);
}

static volatile XhciInterrupterRegs* xhci_intr0_regs(volatile XhciCapabilityRegs* cap) {
    return (volatile XhciInterrupterRegs*)((uintptr_t)cap + cap->rtsoff + 0x20);
}

static volatile uint32_t* xhci_port_regs(volatile XhciCapabilityRegs* cap) {
    return (volatile uint32_t*)((uintptr_t)xhci_op_regs(cap) + 0x400);
}

static volatile uint32_t* xhci_doorbells(volatile XhciCapabilityRegs* cap) {
    return (volatile uint32_t*)((uintptr_t)cap + cap->dboff);
}

static uint32_t xhci_scratchpad_count(volatile XhciCapabilityRegs* cap) {
    uint32_t hcsparams2 = cap->hcsparams2;
    uint32_t lo = (hcsparams2 >> 27) & 0x1F;
    uint32_t hi = (hcsparams2 >> 21) & 0x1F;
    return (hi << 5) | lo;
}

static void xhci_bios_handoff(volatile XhciCapabilityRegs* cap) {
    uint32_t hccparams1 = cap->hccparams1;
    uint32_t ext_offset = ((hccparams1 >> 16) & 0xFFFF) << 2;

    while (ext_offset != 0) {
        volatile uint32_t* ext_cap = (volatile uint32_t*)((uintptr_t)cap + ext_offset);
        uint32_t header = ext_cap[0];
        uint8_t cap_id = (uint8_t)(header & 0xFF);
        uint8_t next_ptr = (uint8_t)((header >> 8) & 0xFF);

        if (cap_id == 1) {
            ext_cap[0] |= 1u << 24;
            for (uint32_t spin = 0; spin < 1000000; spin++) {
                if ((ext_cap[0] & (1u << 16)) == 0) break;
            }
            break;
        }

        if (next_ptr == 0) break;
        ext_offset = (uint32_t)next_ptr << 2;
    }
}

static bool xhci_wait_ready(volatile XhciCapabilityRegs* cap) {
    volatile XhciOperationalRegs* op = xhci_op_regs(cap);
    for (uint32_t spin = 0; spin < 1000000; spin++) {
        if ((op->usbsts & kUsbstsControllerNotReady) == 0) return true;
    }
    return false;
}

static void xhci_snapshot_ports(volatile XhciCapabilityRegs* cap, UsbPortInfo* ports, uint8_t max_ports) {
    volatile uint32_t* port_regs = xhci_port_regs(cap);
    for (uint32_t i = 0; i < max_ports && i < USB_MAX_PORTS; i++) {
        uint32_t portsc = port_regs[i * 4];
        ports[i].connected = (portsc & kPortscCcs) != 0;
        ports[i].enabled = (portsc & kPortscPed) != 0;
        ports[i].powered = (portsc & kPortscPortPower) != 0;
        ports[i].reset_ok = false;
        ports[i].speed_id = (uint8_t)((portsc >> 10) & 0x0F);
        ports[i].slot_id = 0;
        ports[i].enable_slot_ok = false;
    }
}

static void xhci_clear_port_change_bits(volatile uint32_t* port_reg) {
    uint32_t preserve = *port_reg & kPortscWritePreserveMask;
    *port_reg = preserve | kPortscCsc | kPortscPec | kPortscWrc | kPortscOcc | kPortscPrc | kPortscPlc | kPortscCec;
}

static bool xhci_reset_port(volatile uint32_t* port_reg) {
    uint32_t portsc = *port_reg;
    if ((portsc & kPortscCcs) == 0) return false;

    xhci_clear_port_change_bits(port_reg);
    portsc = *port_reg;
    *port_reg = (portsc & kPortscWritePreserveMask) | kPortscPortReset;

    for (uint32_t spin = 0; spin < 4000000; spin++) {
        uint32_t current = *port_reg;
        if ((current & kPortscPortReset) == 0 && (current & kPortscPed) != 0) {
            xhci_clear_port_change_bits(port_reg);
            return true;
        }
    }
    xhci_clear_port_change_bits(port_reg);
    return false;
}

static uint16_t xhci_completion_code(const XhciTrb& trb) {
    return (uint16_t)((trb.status >> 24) & 0xFF);
}

static uint32_t xhci_transfer_residual(const XhciTrb& trb) {
    return trb.status & 0x00FFFFFFu;
}

static bool xhci_event_success(const XhciTrb& trb) {
    uint16_t code = xhci_completion_code(trb);
    return code == 1 || code == 13;
}

static uint16_t xhci_default_ep0_packet_size(uint8_t speed_id) {
    switch (speed_id) {
    case 2: return 8;
    case 3: return 64;
    case 4: return 512;
    case 5: return 512;
    case 1:
    default:
        return 64;
    }
}

static uint8_t* xhci_context_ptr(uint8_t* base, uint32_t index, uint8_t context_size) {
    return base + ((uint64_t)index * context_size);
}

static void xhci_ring_enqueue_transfer(XhciRuntimeState* rt, uint8_t slot_id, const XhciTrb& src, XhciTrb** out_trb) {
    if (slot_id == 0 || slot_id > USB_MAX_SLOTS) {
        if (out_trb) *out_trb = nullptr;
        return;
    }

    XhciTrb* ring = rt->transfer_rings[slot_id - 1];
    if (!ring) {
        if (out_trb) *out_trb = nullptr;
        return;
    }

    uint32_t ring_index = slot_id - 1;
    uint32_t index = rt->transfer_enqueue[ring_index];
    if (index >= kMaxTrbs - 1) index = 0;

    ring[index] = src;
    ring[index].control &= ~kTrbCycle;
    if (rt->transfer_cycles[ring_index]) ring[index].control |= kTrbCycle;

    if (out_trb) *out_trb = &ring[index];

    index++;
    if (index >= kMaxTrbs - 1) {
        index = 0;
        rt->transfer_cycles[ring_index] ^= 1;
    }
    rt->transfer_enqueue[ring_index] = (uint16_t)index;
}

static void xhci_ring_reset_transfer(XhciRuntimeState* rt, uint8_t slot_id) {
    if (slot_id == 0 || slot_id > USB_MAX_SLOTS) return;
    uint32_t ring_index = slot_id - 1;
    XhciTrb* ring = rt->transfer_rings[ring_index];
    if (!ring) return;
    for (uint32_t i = 0; i < kMaxTrbs - 1; i++) {
        mem_zero(&ring[i], sizeof(XhciTrb));
    }
    ring[kMaxTrbs - 1].parameter_lo = (uint32_t)(uintptr_t)ring;
    ring[kMaxTrbs - 1].parameter_hi = (uint32_t)((uint64_t)(uintptr_t)ring >> 32);
    ring[kMaxTrbs - 1].status = 0;
    ring[kMaxTrbs - 1].control = (6u << kTrbTypeShift) | kTrbCycle | kTrbToggleCycle;
    rt->transfer_cycles[ring_index] = 1;
    rt->transfer_enqueue[ring_index] = 0;
}

static bool xhci_slot_resources_ready(XhciRuntimeState* rt, uint8_t slot_id) {
    if (slot_id == 0 || slot_id > USB_MAX_SLOTS) return false;
    uint32_t i = slot_id - 1;
    if (rt->input_contexts[i] && rt->device_contexts[i] && rt->transfer_rings[i]) return true;

    rt->input_contexts[i] = (uint8_t*)alloc_aligned(rt->context_size * 33, 64);
    rt->device_contexts[i] = (uint8_t*)alloc_aligned(rt->context_size * 32, 64);
    rt->transfer_rings[i] = (XhciTrb*)alloc_aligned(sizeof(XhciTrb) * kMaxTrbs, 64);
    if (!rt->input_contexts[i] || !rt->device_contexts[i] || !rt->transfer_rings[i]) return false;

    xhci_ring_reset_transfer(rt, slot_id);
    return true;
}

static void xhci_prepare_slot_input_context(XhciRuntimeState* rt, const UsbPortInfo* port, uint8_t port_index) {
    uint32_t slot_index = port->slot_id - 1;
    uint8_t* input_ctx = rt->input_contexts[slot_index];
    uint8_t* dev_ctx = rt->device_contexts[slot_index];
    XhciTrb* transfer_ring = rt->transfer_rings[slot_index];
    mem_zero(input_ctx, rt->context_size * 33);
    mem_zero(dev_ctx, rt->context_size * 32);
    xhci_ring_reset_transfer(rt, port->slot_id);

    rt->dcbaa[port->slot_id] = (uint64_t)(uintptr_t)dev_ctx;

    uint32_t* input_control = (uint32_t*)xhci_context_ptr(input_ctx, 0, rt->context_size);
    uint32_t* slot_ctx = (uint32_t*)xhci_context_ptr(input_ctx, 1, rt->context_size);
    uint32_t* ep0_ctx = (uint32_t*)xhci_context_ptr(input_ctx, 2, rt->context_size);

    input_control[1] = (1u << 0) | (1u << 1);
    slot_ctx[0] = ((uint32_t)port->speed_id << 20) | (1u << 27);
    slot_ctx[1] = ((uint32_t)port_index << 16);

    uint16_t max_packet = xhci_default_ep0_packet_size(port->speed_id);
    ep0_ctx[1] = (3u << 1) | (4u << 3) | ((uint32_t)max_packet << 16);
    ep0_ctx[2] = ((uint32_t)(uintptr_t)transfer_ring & ~0xFu) | 1u;
    ep0_ctx[3] = (uint32_t)((uint64_t)(uintptr_t)transfer_ring >> 32);
    ep0_ctx[4] = 8;
}

static void xhci_queue_address_device(XhciRuntimeState* rt, uint8_t slot_id) {
    uint32_t index = rt->command_enqueue;
    XhciTrb* trb = &rt->command_ring[index];
    uint8_t* input_ctx = rt->input_contexts[slot_id - 1];
    trb->parameter_lo = (uint32_t)(uintptr_t)input_ctx;
    trb->parameter_hi = (uint32_t)((uint64_t)(uintptr_t)input_ctx >> 32);
    trb->status = 0;
    trb->control = (kTrbTypeAddressDeviceCmd << kTrbTypeShift)
        | ((uint32_t)slot_id << 24)
        | (rt->command_cycle ? kTrbCycle : 0);

    index++;
    if (index >= kMaxTrbs - 1) {
        index = 0;
        rt->command_cycle ^= 1;
    }
    rt->command_enqueue = index;
}

static bool xhci_wait_transfer_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, XhciTrb* target_trb) {
    XhciTrb unused{};
    return xhci_wait_transfer_completion(cap, rt, target_trb, &unused);
}

static bool xhci_wait_transfer_completion(volatile XhciCapabilityRegs* cap,
                                          XhciRuntimeState* rt,
                                          XhciTrb* target_trb,
                                          XhciTrb* out_event) {
    if (!target_trb) return false;
    XhciTrb event{};
    uintptr_t target = (uintptr_t)target_trb;
    for (uint32_t spin = 0; spin < 8000000; spin++) {
        if (!xhci_poll_event(cap, rt, &event)) continue;
        uint32_t trb_type = (event.control >> kTrbTypeShift) & 0x3F;
        if (trb_type != kTrbTypeTransferEvent) continue;

        uintptr_t event_ptr = (uintptr_t)event.parameter_lo | ((uint64_t)event.parameter_hi << 32);
        if (event_ptr != target) continue;
        if (out_event) *out_event = event;
        return xhci_event_success(event);
    }
    return false;
}

static bool xhci_control_in(volatile XhciCapabilityRegs* cap,
                            XhciRuntimeState* rt,
                            uint8_t slot_id,
                            const UsbSetupPacket& setup,
                            void* data_buf,
                            uint8_t* out_completion_code,
                            uint16_t* out_bytes_transferred) {
    if (slot_id == 0 || slot_id > USB_MAX_SLOTS || !data_buf) return false;
    if (out_completion_code) *out_completion_code = 0xFF;
    if (out_bytes_transferred) *out_bytes_transferred = 0;

    uint32_t setup_dw0 = (uint32_t)setup.bmRequestType
        | ((uint32_t)setup.bRequest << 8)
        | ((uint32_t)setup.wValue << 16);
    uint32_t setup_dw1 = (uint32_t)setup.wIndex | ((uint32_t)setup.wLength << 16);

    XhciTrb setup_trb{};
    setup_trb.parameter_lo = setup_dw0;
    setup_trb.parameter_hi = setup_dw1;
    setup_trb.status = 8;
    setup_trb.control = (kTrbTypeSetupStage << kTrbTypeShift) | (3u << 16) | kTrbIdt;

    XhciTrb data_trb{};
    data_trb.parameter_lo = (uint32_t)(uintptr_t)data_buf;
    data_trb.parameter_hi = (uint32_t)((uint64_t)(uintptr_t)data_buf >> 32);
    data_trb.status = setup.wLength;
    data_trb.control = (kTrbTypeDataStage << kTrbTypeShift) | (1u << 16) | kTrbIoc;

    XhciTrb status_trb{};
    status_trb.parameter_lo = 0;
    status_trb.parameter_hi = 0;
    status_trb.status = 0;
    status_trb.control = (kTrbTypeStatusStage << kTrbTypeShift) | kTrbIoc;

    XhciTrb* data_completion_trb = nullptr;
    XhciTrb* completion_trb = nullptr;
    xhci_ring_enqueue_transfer(rt, slot_id, setup_trb, nullptr);
    xhci_ring_enqueue_transfer(rt, slot_id, data_trb, &data_completion_trb);
    xhci_ring_enqueue_transfer(rt, slot_id, status_trb, &completion_trb);
    if (!completion_trb || !data_completion_trb) return false;

    xhci_ring_doorbell(cap, slot_id, 1);
    XhciTrb data_completion{};
    bool data_ok = xhci_wait_transfer_completion(cap, rt, data_completion_trb, &data_completion);
    if (out_completion_code) *out_completion_code = data_ok ? (uint8_t)xhci_completion_code(data_completion) : 0xFF;
    if (data_ok && out_bytes_transferred) {
        uint32_t residual = xhci_transfer_residual(data_completion);
        uint32_t transferred = ((uint32_t)setup.wLength >= residual) ? ((uint32_t)setup.wLength - residual) : 0;
        if (transferred > 0xFFFFu) transferred = 0xFFFFu;
        *out_bytes_transferred = (uint16_t)transferred;
    }

    if (!data_ok) return false;

    XhciTrb status_completion{};
    bool status_ok = xhci_wait_transfer_completion(cap, rt, completion_trb, &status_completion);
    if (!status_ok) {
        if (out_completion_code) *out_completion_code = 0xFE;
        return false;
    }
    return true;
}

static bool xhci_fetch_descriptors(volatile XhciCapabilityRegs* cap,
                                   XhciRuntimeState* rt,
                                   UsbDeviceInfo* dev) {
    if (!dev || dev->slot_id == 0) return false;

    UsbDeviceDescriptor* device_desc = (UsbDeviceDescriptor*)alloc_aligned(sizeof(UsbDeviceDescriptor), 64);
    uint8_t* config_buf = (uint8_t*)alloc_aligned(256, 64);
    if (!device_desc || !config_buf) return false;
    mem_zero(device_desc, sizeof(UsbDeviceDescriptor));
    mem_zero(config_buf, 256);

    UsbSetupPacket setup{};
    setup.bmRequestType = kUsbRequestTypeDeviceToHost | kUsbRequestTypeStandard | kUsbRequestRecipientDevice;
    setup.bRequest = kUsbRequestGetDescriptor;
    setup.wValue = (uint16_t)(kUsbDescriptorTypeDevice << 8);
    setup.wIndex = 0;
    setup.wLength = sizeof(UsbDeviceDescriptor);
    if (!xhci_control_in(cap, rt, dev->slot_id, setup, device_desc, &dev->descriptor_completion_code, &dev->device_bytes_transferred)) return false;
    if (device_desc->bLength < 8 || device_desc->bDescriptorType != kUsbDescriptorTypeDevice) return false;

    dev->descriptor_ok = true;
    dev->usb_class = device_desc->bDeviceClass;
    dev->usb_subclass = device_desc->bDeviceSubClass;
    dev->usb_protocol = device_desc->bDeviceProtocol;
    dev->interface_class = 0;
    dev->interface_subclass = 0;
    dev->interface_protocol = 0;
    dev->interface_count = 0;
    dev->config_descriptor_count = 0;
    dev->config_header_len = 0;
    dev->config_header_type = 0;
    for (uint32_t i = 0; i < 8; i++) dev->config_header_bytes[i] = 0;
    dev->device_bytes_transferred = 0;
    dev->config_bytes_transferred = 0;
    dev->config_total_length = 0;
    for (uint32_t i = 0; i < 8; i++) dev->config_first_types[i] = 0;
    dev->vendor_id = device_desc->idVendor;
    dev->product_id = device_desc->idProduct;

    setup.wValue = (uint16_t)(kUsbDescriptorTypeConfiguration << 8);
    setup.wLength = kUsbConfigDescriptorLength;
    if (!xhci_control_in(cap, rt, dev->slot_id, setup, config_buf, &dev->config_completion_code, &dev->config_bytes_transferred)) return true;
    for (uint32_t i = 0; i < 8; i++) dev->config_header_bytes[i] = config_buf[i];
    dev->config_header_len = config_buf[0];
    dev->config_header_type = config_buf[1];
    if (config_buf[0] < kUsbConfigDescriptorLength || config_buf[1] != kUsbDescriptorTypeConfiguration) return true;

    uint16_t total_length = (uint16_t)config_buf[2] | ((uint16_t)config_buf[3] << 8);
    dev->config_total_length = total_length;
    if (total_length > 256) total_length = 256;
    if (total_length < kUsbConfigDescriptorLength) return true;

    mem_zero(config_buf, 256);
    setup.wLength = total_length;
    if (!xhci_control_in(cap, rt, dev->slot_id, setup, config_buf, &dev->config_completion_code, &dev->config_bytes_transferred)) return true;

    static constexpr uint8_t kUsbDescriptorTypeEndpoint = 5;
    static constexpr uint8_t kUsbEndpointDirIn = 0x80;
    static constexpr uint8_t kUsbEndpointTypebulk = 2;

    // w xhci_fetch_descriptors, zamiast starej pętli:
    uint16_t offset = 0;
    bool in_mass_storage_interface = false;
    while (offset + 2 <= total_length) {
        uint8_t len = config_buf[offset];
        uint8_t type = config_buf[offset + 1];
        if (len < 2 || offset + len > total_length) break;

        if (dev->config_descriptor_count < 8)
            dev->config_first_types[dev->config_descriptor_count] = type;
        dev->config_descriptor_count++;

        if (type == kUsbDescriptorTypeInterface && len >= kUsbInterfaceDescriptorLength) {
            dev->interface_count++;
            uint8_t iclass = config_buf[offset + 5];
            uint8_t isubclass = config_buf[offset + 6];
            uint8_t iproto = config_buf[offset + 7];
            if (dev->interface_count == 1) {
                dev->interface_class = iclass;
                dev->interface_subclass = isubclass;
                dev->interface_protocol = iproto;
            }
            in_mass_storage_interface = (iclass == 0x08);
            if (in_mass_storage_interface) {
                dev->is_mass_storage = true;
                dev->usb_class = iclass;
                dev->usb_subclass = isubclass;
                dev->usb_protocol = iproto;
                dev->configuration_value = config_buf[5]; // bConfigurationValue
            }
        }
        else if (type == kUsbDescriptorTypeEndpoint && len >= 7 && in_mass_storage_interface) {
            uint8_t  addr = config_buf[offset + 2];
            uint8_t  attr = config_buf[offset + 3];
            uint16_t mps = (uint16_t)config_buf[offset + 4]
                | ((uint16_t)config_buf[offset + 5] << 8);
            bool is_in = (addr & kUsbEndpointDirIn) != 0;
            bool is_bulk = (attr & 0x03) == kUsbEndpointTypeBulk;
            if (is_bulk && is_in && dev->bulk_in_endpoint == 0) {
                dev->bulk_in_endpoint = addr & 0x7F;
                dev->bulk_in_max_packet = mps;
            }
            if (is_bulk && !is_in && dev->bulk_out_endpoint == 0) {
                dev->bulk_out_endpoint = addr & 0x7F;
                dev->bulk_out_max_packet = mps;
            }
        }
        offset = (uint16_t)(offset + len);
    }
    return true;
}

static bool xhci_prepare_runtime(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbXhciControllerInfo* info) {
    mem_zero(rt, sizeof(XhciRuntimeState));
    rt->command_cycle = 1;
    rt->event_cycle = 1;
    rt->context_size = (cap->hccparams1 & (1u << 2)) ? 64 : 32;

    uint32_t max_slots = info->max_slots;
    if (max_slots == 0) max_slots = 1;
    rt->dcbaa = (uint64_t*)alloc_aligned((max_slots + 1) * sizeof(uint64_t), 64);
    rt->command_ring = (XhciTrb*)alloc_aligned(sizeof(XhciTrb) * kMaxTrbs, 64);
    rt->event_ring = (XhciTrb*)alloc_aligned(sizeof(XhciTrb) * kMaxTrbs, 64);
    rt->erst = (XhciErstEntry*)alloc_aligned(sizeof(XhciErstEntry) * kEventRingSegmentCount, 64);
    if (!rt->dcbaa || !rt->command_ring || !rt->event_ring || !rt->erst) return false;

    rt->scratchpad_count = xhci_scratchpad_count(cap);
    if (rt->scratchpad_count > kUsbMaxScratchpads) rt->scratchpad_count = kUsbMaxScratchpads;
    if (rt->scratchpad_count > 0) {
        rt->scratchpad_array = (uint64_t*)alloc_aligned(rt->scratchpad_count * sizeof(uint64_t), 64);
        if (!rt->scratchpad_array) return false;
        for (uint32_t i = 0; i < rt->scratchpad_count; i++) {
            rt->scratchpad_buffers[i] = (uint8_t*)alloc_aligned(4096, 4096);
            if (!rt->scratchpad_buffers[i]) return false;
            rt->scratchpad_array[i] = (uint64_t)(uintptr_t)rt->scratchpad_buffers[i];
        }
        rt->dcbaa[0] = (uint64_t)(uintptr_t)rt->scratchpad_array;
    }

    rt->command_ring[kMaxTrbs - 1].parameter_lo = (uint32_t)(uintptr_t)rt->command_ring;
    rt->command_ring[kMaxTrbs - 1].parameter_hi = (uint32_t)((uint64_t)(uintptr_t)rt->command_ring >> 32);
    rt->command_ring[kMaxTrbs - 1].status = 0;
    rt->command_ring[kMaxTrbs - 1].control = (6u << kTrbTypeShift) | kTrbCycle | kTrbToggleCycle;

    rt->erst[0].ring_segment_base = (uint64_t)(uintptr_t)rt->event_ring;
    rt->erst[0].ring_segment_size = kMaxTrbs;
    rt->resources_ready = true;
    return true;
}

static void xhci_setup_rings(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbXhciControllerInfo* info) {
    volatile XhciOperationalRegs* op = xhci_op_regs(cap);
    volatile XhciInterrupterRegs* intr = xhci_intr0_regs(cap);

    op->dcbaap = (uint64_t)(uintptr_t)rt->dcbaa;
    op->config = (op->config & ~kConfigMaxSlotsMask) | info->max_slots;
    op->crcr = ((uint64_t)(uintptr_t)rt->command_ring) | 1ull;

    intr->erstsz = 1;
    intr->erstba = (uint64_t)(uintptr_t)rt->erst;
    intr->erdp = (uint64_t)(uintptr_t)rt->event_ring;
    intr->iman = 2;

    uint32_t cmd = op->usbcmd;
    cmd |= kUsbcmdInterrupterEnable;
    cmd |= kUsbcmdRunStop;
    op->usbcmd = cmd;

    for (uint32_t spin = 0; spin < 1000000; spin++) {
        if ((op->usbsts & kUsbstsHcHalted) == 0) break;
    }
    rt->command_ring_running = true;
}

static void xhci_ring_doorbell(volatile XhciCapabilityRegs* cap, uint32_t index, uint32_t value) {
    volatile uint32_t* db = xhci_doorbells(cap);
    db[index] = value;
}

static void xhci_queue_enable_slot(XhciRuntimeState* rt) {
    uint32_t index = rt->command_enqueue;
    XhciTrb* trb = &rt->command_ring[index];
    trb->parameter_lo = 0;
    trb->parameter_hi = 0;
    trb->status = 0;
    trb->control = (kTrbTypeEnableSlotCmd << kTrbTypeShift) | (rt->command_cycle ? kTrbCycle : 0);

    index++;
    if (index >= kMaxTrbs - 1) {
        index = 0;
        rt->command_cycle ^= 1;
    }
    rt->command_enqueue = index;
}

static bool xhci_poll_event(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, XhciTrb* out_trb) {
    XhciTrb* trb = &rt->event_ring[rt->event_dequeue];
    bool cycle = (trb->control & kTrbCycle) != 0;
    if (cycle != (rt->event_cycle != 0)) return false;

    mem_copy(out_trb, trb, sizeof(XhciTrb));

    rt->event_dequeue++;
    if (rt->event_dequeue >= kMaxTrbs) {
        rt->event_dequeue = 0;
        rt->event_cycle ^= 1;
    }

    volatile XhciInterrupterRegs* intr = xhci_intr0_regs(cap);
    intr->erdp = ((uint64_t)(uintptr_t)&rt->event_ring[rt->event_dequeue]) | (1ull << 3);
    return true;
}

static bool xhci_wait_command_completion(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, uint8_t* out_slot_id) {
    XhciTrb unused{};
    return xhci_wait_command_completion(cap, rt, out_slot_id, &unused);
}

static bool xhci_wait_command_completion(volatile XhciCapabilityRegs* cap,
                                         XhciRuntimeState* rt,
                                         uint8_t* out_slot_id,
                                         XhciTrb* out_event) {
    XhciTrb event{};
    for (uint32_t spin = 0; spin < 4000000; spin++) {
        if (!xhci_poll_event(cap, rt, &event)) continue;

        uint32_t trb_type = (event.control >> kTrbTypeShift) & 0x3F;
        if (trb_type == kTrbTypeCommandCompletion) {
            if (out_slot_id) *out_slot_id = (uint8_t)((event.control >> 24) & 0xFF);
            if (out_event) *out_event = event;
            return xhci_event_success(event);
        }
    }
    return false;
}

static void xhci_try_enable_slots(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbXhciControllerInfo* info) {
    if (!rt->command_ring_running) return;
    volatile uint32_t* port_regs = xhci_port_regs(cap);

    for (uint32_t p = 0; p < info->max_ports && p < USB_MAX_PORTS; p++) {
        UsbPortInfo* port = &info->ports_after_handoff[p];
        if (!port->connected) continue;
        port->reset_ok = xhci_reset_port(&port_regs[p * 4]);
        uint32_t portsc = port_regs[p * 4];
        port->connected = (portsc & kPortscCcs) != 0;
        port->enabled = (portsc & kPortscPed) != 0;
        port->powered = (portsc & kPortscPortPower) != 0;
        port->speed_id = (uint8_t)((portsc >> 10) & 0x0F);
        if (!port->connected || !port->reset_ok) continue;

        xhci_queue_enable_slot(rt);
        xhci_ring_doorbell(cap, 0, 0);

        uint8_t slot_id = 0;
        if (xhci_wait_command_completion(cap, rt, &slot_id) && slot_id != 0) {
            port->enable_slot_ok = true;
            port->slot_id = slot_id;
        }
    }
}
static bool xhci_configure_bulk_endpoints(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt,
    UsbDeviceInfo* dev) {

    if (!dev->bulk_in_endpoint || !dev->bulk_out_endpoint) return false;
    uint8_t slot_id = dev->slot_id;
    if (slot_id == 0 || slot_id > USB_MAX_SLOTS) return false;
    uint32_t si = slot_id - 1;

    // Wylicz indeksy EP context
    uint8_t  ep_out_num = dev->bulk_out_endpoint;       // np. 1
    uint8_t  ep_in_num = dev->bulk_in_endpoint;        // np. 2
    uint32_t ep_out_ctx_idx = (uint32_t)ep_out_num * 2;     // np. 2
    uint32_t ep_in_ctx_idx = (uint32_t)ep_in_num * 2 + 1; // np. 5
    uint32_t max_ep_idx = (ep_in_ctx_idx > ep_out_ctx_idx) ? ep_in_ctx_idx : ep_out_ctx_idx;

    // Zapisz diagnostykę
    dev->bulk_cfg_ep_out_ctx = ep_out_ctx_idx;
    dev->bulk_cfg_ep_in_ctx = ep_in_ctx_idx;
    dev->bulk_cfg_entries = max_ep_idx;

    // Alokuj ringi dla bulk OUT (idx 0) i bulk IN (idx 1)
    for (uint32_t d = 0; d < 2; d++) {
        rt->bulk_rings[si][d] = (XhciTrb*)alloc_aligned(sizeof(XhciTrb) * kMaxTrbs, 64);
        if (!rt->bulk_rings[si][d]) return false;
        rt->bulk_cycles[si][d] = 1;
        rt->bulk_enqueue[si][d] = 0;
        XhciTrb* r = rt->bulk_rings[si][d];
        for (uint32_t i = 0; i < kMaxTrbs - 1; i++) mem_zero(&r[i], sizeof(XhciTrb));
        r[kMaxTrbs - 1].parameter_lo = (uint32_t)(uintptr_t)r;
        r[kMaxTrbs - 1].parameter_hi = (uint32_t)((uint64_t)(uintptr_t)r >> 32);
        r[kMaxTrbs - 1].status = 0;
        r[kMaxTrbs - 1].control = (6u << kTrbTypeShift) | kTrbCycle | kTrbToggleCycle;
    }

    // Przygotuj Input Context
    uint8_t* input_ctx = rt->input_contexts[si];
    uint8_t* dev_ctx = rt->device_contexts[si];
    mem_zero(input_ctx, rt->context_size * 33);

    // Input Control Context (index 0)
    uint32_t* icc = (uint32_t*)xhci_context_ptr(input_ctx, 0, rt->context_size);
    icc[0] = 0;
    icc[1] = (1u << 0)               // slot
        | (1u << ep_out_ctx_idx)  // bulk OUT
        | (1u << ep_in_ctx_idx);  // bulk IN
    dev->bulk_cfg_icc1 = icc[1];

    // Slot Context (index 1) — kopiuj z device context slot 0
    mem_copy(xhci_context_ptr(input_ctx, 1, rt->context_size),
        xhci_context_ptr(dev_ctx, 0, rt->context_size),
        rt->context_size);
    uint32_t* slot_ctx_in = (uint32_t*)xhci_context_ptr(input_ctx, 1, rt->context_size);
    slot_ctx_in[0] &= ~(0x1Fu << 27);
    slot_ctx_in[0] |= (max_ep_idx << 27);  // Context Entries

    // EP Bulk OUT context
    uint32_t* ep_out = (uint32_t*)xhci_context_ptr(input_ctx, ep_out_ctx_idx + 1, rt->context_size);
    XhciTrb* out_ring = rt->bulk_rings[si][0];
    ep_out[0] = 0;
    // Dodano `| (3u << 1)` na końcu:
    ep_out[1] = (2u << 3) | ((uint32_t)dev->bulk_out_max_packet << 16) | (3u << 1);
    ep_out[2] = ((uint32_t)(uintptr_t)out_ring & ~0xFu) | 1u;
    ep_out[3] = (uint32_t)((uint64_t)(uintptr_t)out_ring >> 32);
    ep_out[4] = dev->bulk_out_max_packet;

    // EP Bulk IN context
    uint32_t* ep_in = (uint32_t*)xhci_context_ptr(input_ctx, ep_in_ctx_idx + 1, rt->context_size);
    XhciTrb* in_ring = rt->bulk_rings[si][1];
    ep_in[0] = 0;
    // Dodano `| (3u << 1)` na końcu:
    ep_in[1] = (6u << 3) | ((uint32_t)dev->bulk_in_max_packet << 16) | (3u << 1);
    ep_in[2] = ((uint32_t)(uintptr_t)in_ring & ~0xFu) | 1u;
    dev->dbg_cfg_in_ring_lo = (uint32_t)(uintptr_t)in_ring;
    ep_in[3] = (uint32_t)((uint64_t)(uintptr_t)in_ring >> 32);
    ep_in[4] = dev->bulk_in_max_packet;

    // Queue Configure Endpoint Command
    uint32_t index = rt->command_enqueue;
    XhciTrb* trb = &rt->command_ring[index];
    trb->parameter_lo = (uint32_t)(uintptr_t)input_ctx;
    trb->parameter_hi = (uint32_t)((uint64_t)(uintptr_t)input_ctx >> 32);
    trb->status = 0;
    trb->control = (kTrbTypeConfigureEndpointCmd << kTrbTypeShift)
        | ((uint32_t)slot_id << 24)
        | (rt->command_cycle ? kTrbCycle : 0);
    index++;
    if (index >= kMaxTrbs - 1) { index = 0; rt->command_cycle ^= 1; }
    rt->command_enqueue = index;

    xhci_ring_doorbell(cap, 0, 0);
    XhciTrb completion{};
    bool ok = xhci_wait_command_completion(cap, rt, nullptr, &completion);
    dev->bulk_cfg_completion_code = (uint8_t)xhci_completion_code(completion);
    return ok;
}
static constexpr uint32_t kBotCbwSignature = 0x43425355; // 'USBC'
static constexpr uint32_t kBotCswSignature = 0x53425355; // 'USBS'
static constexpr uint32_t kBotCbwLen = 31;
static constexpr uint32_t kBotCswLen = 13;

struct BotCbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;       // 0x80 = IN, 0x00 = OUT
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
};

struct BotCsw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;       // 0=ok, 1=fail, 2=phase error
};

static uint32_t g_bot_tag = 1;
static bool xhci_bulk_out(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt,
    UsbDeviceInfo* dev,
    void* buf, uint32_t len) {
    uint8_t slot_id = dev->slot_id;
    uint32_t si = slot_id - 1;
    XhciTrb* ring = rt->bulk_rings[si][0];
    if (!ring) return false;

    uint32_t idx = rt->bulk_enqueue[si][0];
    XhciTrb* trb = &ring[idx];
    mem_zero(trb, sizeof(XhciTrb));
    trb->parameter_lo = (uint32_t)(uintptr_t)buf;
    trb->parameter_hi = (uint32_t)((uint64_t)(uintptr_t)buf >> 32);
    trb->status = len;
    trb->control = (1u << kTrbTypeShift) | kTrbIoc
        | (rt->bulk_cycles[si][0] ? kTrbCycle : 0);

    // zapisz diagnostykę PRZED inkrementacją
    dev->dbg_ring_lo = (uint32_t)(uintptr_t)ring;
    dev->dbg_ring_hi = (uint32_t)((uint64_t)(uintptr_t)ring >> 32);
    dev->dbg_idx = idx;
    dev->dbg_target_lo = (uint32_t)(uintptr_t)trb;
    dev->dbg_target_hi = (uint32_t)((uint64_t)(uintptr_t)trb >> 32);

    idx++;
    if (idx >= kMaxTrbs - 1) { idx = 0; rt->bulk_cycles[si][0] ^= 1; }
    rt->bulk_enqueue[si][0] = (uint16_t)idx;

    xhci_ring_doorbell(cap, slot_id, dev->bulk_out_endpoint * 2);

    XhciTrb event{};
    uintptr_t target = (uintptr_t)trb;
    for (uint32_t spin = 0; spin < 8000000; spin++) {
        if (!xhci_poll_event(cap, rt, &event)) continue;
        uint32_t trb_type = (event.control >> kTrbTypeShift) & 0x3F;
        if (trb_type != kTrbTypeTransferEvent) continue;
        uintptr_t event_ptr = (uintptr_t)event.parameter_lo
            | ((uint64_t)event.parameter_hi << 32);
        dev->dbg_event_type = trb_type;
        dev->dbg_event_ptr_lo = event.parameter_lo;
        dev->dbg_event_ptr_hi = event.parameter_hi;
        dev->dbg_event_cc = (uint8_t)xhci_completion_code(event);
        dev->bot_bulk_out_cc = dev->dbg_event_cc;
        if (event_ptr != target) continue;
        return xhci_event_success(event);
    }
    dev->bot_bulk_out_cc = 0;
    return false;
}
static constexpr uint32_t kTrbTypeResetEndpointCmd = 14;

static void xhci_reset_endpoint(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt,
    uint8_t slot_id,
    uint32_t ep_ctx_idx) {
    uint32_t index = rt->command_enqueue;
    XhciTrb* trb = &rt->command_ring[index];
    trb->parameter_lo = 0;
    trb->parameter_hi = 0;
    trb->status = 0;
    trb->control = (kTrbTypeResetEndpointCmd << kTrbTypeShift)
        | ((uint32_t)slot_id << 24)
        | (ep_ctx_idx << 16)
        | (rt->command_cycle ? kTrbCycle : 0);
    index++;
    if (index >= kMaxTrbs - 1) { index = 0; rt->command_cycle ^= 1; }
    rt->command_enqueue = index;
    xhci_ring_doorbell(cap, 0, 0);
    XhciTrb completion{};
    xhci_wait_command_completion(cap, rt, nullptr, &completion);
}
static bool xhci_bulk_in(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt,
    UsbDeviceInfo* dev,
    void* buf, uint32_t len,
    uint32_t* out_received) {
    uint8_t slot_id = dev->slot_id;
    uint32_t si = slot_id - 1;
    XhciTrb* ring = rt->bulk_rings[si][1];
    if (!ring) return false;
    if (out_received) *out_received = 0;

    uint32_t idx = rt->bulk_enqueue[si][1];
    XhciTrb* trb = &ring[idx];
    mem_zero(trb, sizeof(XhciTrb));
    trb->parameter_lo = (uint32_t)(uintptr_t)buf;
    trb->parameter_hi = (uint32_t)((uint64_t)(uintptr_t)buf >> 32);
    trb->status = len;

    trb->control = (1u << kTrbTypeShift) | kTrbIoc
        | (rt->bulk_cycles[si][1] ? kTrbCycle : 0);

    // zapisz target PRZED inkrementacją
    uintptr_t target = (uintptr_t)trb;
    dev->dbg_in_ring_lo = (uint32_t)(uintptr_t)ring;
    dev->dbg_in_ring_hi = (uint32_t)((uint64_t)(uintptr_t)ring >> 32);
    idx++;
    if (idx >= kMaxTrbs - 1) { idx = 0; rt->bulk_cycles[si][1] ^= 1; }
    rt->bulk_enqueue[si][1] = (uint16_t)idx;
    dev->dbg_in_target_lo = (uint32_t)target;
    dev->dbg_in_target_hi = (uint32_t)((uint64_t)target >> 32);
    dev->dbg_in_idx = idx - 1; // idx już zinkrementowany, cofnij o 1
    //xhci_reset_endpoint(cap, rt, slot_id, dev->bulk_in_endpoint * 2 + 1);
    dev->dbg_in_trb_ctrl = trb->control;
    dev->dbg_in_trb_stat = trb->status;
    dev->dbg_in_trb_p0 = trb->parameter_lo;
    xhci_ring_doorbell(cap, slot_id, dev->bulk_in_endpoint * 2 + 1);

    XhciTrb event{};
    for (uint32_t spin = 0; spin < 8000000; spin++) {
        if (!xhci_poll_event(cap, rt, &event)) continue;
        uint32_t trb_type = (event.control >> kTrbTypeShift) & 0x3F;
        dev->dbg_in_event_type = trb_type;
        dev->dbg_in_event_ptr_lo = event.parameter_lo;
        dev->dbg_in_event_ptr_hi = event.parameter_hi;
        dev->bot_bulk_in_cc = (uint8_t)xhci_completion_code(event);
        if (trb_type != kTrbTypeTransferEvent) continue;
        uintptr_t event_ptr = (uintptr_t)event.parameter_lo
            | ((uint64_t)event.parameter_hi << 32);
        if (event_ptr != target) continue;
        if (!xhci_event_success(event)) return false;
        if (out_received) {
            uint32_t residual = xhci_transfer_residual(event);
            *out_received = (len >= residual) ? (len - residual) : 0;
        }
        return true;
    }
    dev->bot_bulk_in_cc = 0;
    return false;
}
alignas(64) static uint8_t g_bot_buf[4096];
alignas(64) static BotCbw g_bot_cbw;
alignas(64) static BotCsw g_bot_csw;
static bool bot_transaction(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt, UsbDeviceInfo* dev, BotCbw* cbw,
    void* data_buf, uint32_t data_len, bool data_in, uint32_t* out_received) {
    
    cbw->dCBWSignature = kBotCbwSignature;
    cbw->dCBWTag = g_bot_tag++;
    cbw->dCBWDataTransferLength = data_len;
    cbw->bmCBWFlags = data_in ? 0x80 : 0x00;

    dev->bot_cbw_phase = 0;
    if (!xhci_bulk_out(cap, rt, dev, cbw, kBotCbwLen)) return false;
    dev->bot_cbw_phase = 1;

    bool data_ok = true;
    if (data_len > 0 && data_buf) {
        if (data_in)
            data_ok = xhci_bulk_in(cap, rt, dev, data_buf, data_len, out_received);
        else
            data_ok = xhci_bulk_out(cap, rt, dev, data_buf, data_len);
        if (!data_ok) return false;
    }
    dev->bot_cbw_phase = 2;

    // --- ZMIANA TUTAJ ---
    BotCsw* csw = &g_bot_csw;
    mem_zero(csw, kBotCswLen);
    
    uint32_t csw_received = 0;
    if (!xhci_bulk_in(cap, rt, dev, csw, kBotCswLen, &csw_received)) return false;
    dev->bot_cbw_phase = 3;

    dev->bot_csw_sig = csw->dCSWSignature;
    dev->bot_csw_status = csw->bCSWStatus;

    if (csw->dCSWSignature != kBotCswSignature) return false;
    if (csw->bCSWStatus != 0) return false;
    dev->bot_cbw_phase = 4;
    return true;
}
static bool scsi_inquiry(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbDeviceInfo* dev) {
    BotCbw* cbw = &g_bot_cbw; // <-- ZMIANA
    uint8_t* buf = g_bot_buf; // <-- ZMIANA
    mem_zero(cbw, kBotCbwLen);
    mem_zero(buf, 36);
    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = 6;
    cbw->CBWCB[0] = 0x12; // INQUIRY
    cbw->CBWCB[4] = 36;
    return bot_transaction(cap, rt, dev, cbw, buf, 36, true, nullptr);
}

static bool scsi_read_capacity(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbDeviceInfo* dev, uint32_t* out_lba, uint32_t* out_block_size) {
    BotCbw* cbw = &g_bot_cbw; // <-- ZMIANA
    uint8_t* buf = g_bot_buf; // <-- ZMIANA
    mem_zero(cbw, kBotCbwLen);
    mem_zero(buf, 8);
    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = 10;
    cbw->CBWCB[0] = 0x25; // READ CAPACITY (10)
    uint32_t received = 0;
    if (!bot_transaction(cap, rt, dev, cbw, buf, 8, true, &received)) return false;
    if (received < 8) return false;

    if (out_lba)        *out_lba = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    if (out_block_size) *out_block_size = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | buf[7];
    return true;
}

static bool scsi_read10(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt, UsbDeviceInfo* dev,
    uint32_t lba, uint16_t block_count, uint32_t block_size, void* out_buf) {

    BotCbw* cbw = &g_bot_cbw; // <-- ZMIANA
    mem_zero(cbw, kBotCbwLen);

    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = 10;
    cbw->CBWCB[0] = 0x28; // READ (10)
    cbw->CBWCB[2] = (uint8_t)(lba >> 24);
    cbw->CBWCB[3] = (uint8_t)(lba >> 16);
    cbw->CBWCB[4] = (uint8_t)(lba >> 8);
    cbw->CBWCB[5] = (uint8_t)(lba);
    cbw->CBWCB[7] = (uint8_t)(block_count >> 8);
    cbw->CBWCB[8] = (uint8_t)(block_count);
    uint32_t total = block_count * block_size;
    uint32_t received = 0;
    return bot_transaction(cap, rt, dev, cbw, out_buf, total, true, &received);
}
static bool xhci_control_no_data(volatile XhciCapabilityRegs* cap,
    XhciRuntimeState* rt,
    uint8_t slot_id,
    const UsbSetupPacket& setup) {
    uint32_t setup_dw0 = (uint32_t)setup.bmRequestType
        | ((uint32_t)setup.bRequest << 8)
        | ((uint32_t)setup.wValue << 16);
    uint32_t setup_dw1 = (uint32_t)setup.wIndex | ((uint32_t)setup.wLength << 16);

    XhciTrb setup_trb{};
    setup_trb.parameter_lo = setup_dw0;
    setup_trb.parameter_hi = setup_dw1;
    setup_trb.status = 8;
    // TRT = 0 (No Data Stage). Brak fazy danych.
    setup_trb.control = (kTrbTypeSetupStage << kTrbTypeShift) | (0u << 16) | kTrbIdt;

    XhciTrb status_trb{};
    status_trb.parameter_lo = 0;
    status_trb.parameter_hi = 0;
    status_trb.status = 0;
    // DIR = 1 (IN) dla Status Stage, jeśli TRT to No Data Stage.
    status_trb.control = (kTrbTypeStatusStage << kTrbTypeShift) | (1u << 16) | kTrbIoc;

    XhciTrb* completion_trb = nullptr;
    xhci_ring_enqueue_transfer(rt, slot_id, setup_trb, nullptr);
    xhci_ring_enqueue_transfer(rt, slot_id, status_trb, &completion_trb);
    if (!completion_trb) return false;

    xhci_ring_doorbell(cap, slot_id, 1);

    XhciTrb status_completion{};
    return xhci_wait_transfer_completion(cap, rt, completion_trb, &status_completion);
}
static void xhci_build_device_list(volatile XhciCapabilityRegs* cap, XhciRuntimeState* rt, UsbXhciControllerInfo* info) {
    info->device_count = 0;
    for (uint32_t i = 0; i < USB_MAX_DEVICES; i++) {
        mem_zero(&info->devices[i], sizeof(UsbDeviceInfo));
    }

    for (uint32_t p = 0; p < info->max_ports && p < USB_MAX_PORTS; p++) {
        const UsbPortInfo* port = &info->ports_after_handoff[p];
        if (!port->connected) continue;
        if (info->device_count >= USB_MAX_DEVICES) break;

        UsbDeviceInfo* dev = &info->devices[info->device_count++];
        dev->present = true;
        dev->addressed = false;
        dev->descriptor_ok = false;
        dev->is_mass_storage = false;
        dev->port_index = (uint8_t)(p + 1);
        dev->slot_id = port->slot_id;
        dev->speed_id = port->speed_id;
        dev->usb_class = 0;
        dev->usb_subclass = 0;
        dev->usb_protocol = 0;
        dev->interface_class = 0;
        dev->interface_subclass = 0;
        dev->interface_protocol = 0;
        dev->interface_count = 0;
        dev->address_completion_code = 0;
        dev->descriptor_completion_code = 0;
        dev->config_completion_code = 0;
        dev->config_descriptor_count = 0;
        dev->config_header_len = 0;
        dev->config_header_type = 0;
        for (uint32_t t = 0; t < 8; t++) dev->config_header_bytes[t] = 0;
        dev->config_total_length = 0;
        for (uint32_t t = 0; t < 8; t++) dev->config_first_types[t] = 0;
        dev->vendor_id = 0;
        dev->product_id = 0;

        if (!port->enable_slot_ok || !port->reset_ok || port->slot_id == 0) continue;
        if (!xhci_slot_resources_ready(rt, port->slot_id)) continue;

        xhci_prepare_slot_input_context(rt, port, (uint8_t)(p + 1));
        xhci_queue_address_device(rt, port->slot_id);
        xhci_ring_doorbell(cap, 0, 0);
        XhciTrb completion{};
        if (!xhci_wait_command_completion(cap, rt, nullptr, &completion)) {
            dev->address_completion_code = (uint8_t)xhci_completion_code(completion);
            continue;
        }

        dev->addressed = true;
        if (!xhci_fetch_descriptors(cap, rt, dev) && dev->descriptor_completion_code == 0)
            dev->descriptor_completion_code = 0xFF;

        // DODAJ:
        if (dev->is_mass_storage && dev->bulk_in_endpoint && dev->bulk_out_endpoint)
        {
            dev->bulk_configured = xhci_configure_bulk_endpoints(cap, rt, dev);
            if (dev->bulk_configured) {
                // WYSŁANIE SET_CONFIGURATION DO URZĄDZENIA
                UsbSetupPacket set_cfg{};
                set_cfg.bmRequestType = 0x00; // Host-to-Device | Standard | Device
                set_cfg.bRequest = 0x09;      // SET_CONFIGURATION
                set_cfg.wValue = dev->configuration_value;
                set_cfg.wIndex = 0;
                set_cfg.wLength = 0;
                xhci_control_no_data(cap, rt, dev->slot_id, set_cfg);

                // Teraz urządzenie ma aktywne Bulk Endpoints, kontynuuj BOT
                uint32_t last_lba = 0, block_size = 0;
                if (scsi_read_capacity(cap, rt, dev, &last_lba, &block_size)) {
                    dev->disk_last_lba = last_lba;
                    dev->disk_block_size = block_size;
                    uint8_t* sector = (uint8_t*)alloc_aligned(block_size, 64);
                    if (sector) {
                        dev->disk_read_ok = scsi_read10(cap, rt, dev, 0, 1, block_size, sector);
                        if (dev->disk_read_ok) {
                            for (uint32_t b = 0; b < 512 && b < block_size; b++)
                                dev->disk_sector0[b] = sector[b];
                        }
                    }
                }
            }
        }
    }
}
bool usb_read_disk_sector(uint32_t device_index, uint64_t lba, void* buffer) {
    // Znajdź kontroler i urządzenie
    for (uint32_t ci = 0; ci < g_xhci_count; ci++) {
        UsbXhciControllerInfo* info = &g_xhci[ci];
        XhciRuntimeState* rt = &g_xhci_runtime[ci];
        volatile XhciCapabilityRegs* cap = (volatile XhciCapabilityRegs*)(uintptr_t)info->mmio_base;
        for (uint32_t di = 0; di < info->device_count; di++) {
            UsbDeviceInfo* dev = &info->devices[di];
            if (!dev->present || !dev->is_mass_storage || !dev->bulk_configured) continue;
            if (dev->disk_block_size == 0) continue;
            if (device_index > 0) { device_index--; continue; }
            return scsi_read10(cap, rt, dev, (uint32_t)lba, 1, dev->disk_block_size, buffer);
        }
    }
    return false;
}
void usb_register_storage_disks() {
    uint32_t usb_dev_idx = 0;
    for (uint32_t ci = 0; ci < g_xhci_count; ci++) {
        UsbXhciControllerInfo* info = &g_xhci[ci];
        XhciRuntimeState* rt = &g_xhci_runtime[ci];
        volatile XhciCapabilityRegs* cap = (volatile XhciCapabilityRegs*)(uintptr_t)info->mmio_base;
        for (uint32_t di = 0; di < info->device_count; di++) {
            UsbDeviceInfo* dev = &info->devices[di];
            if (!dev->present || !dev->is_mass_storage || !dev->bulk_configured) continue;
            if (dev->disk_block_size == 0) continue;
            storage_register_usb_disk(usb_dev_idx, dev->disk_last_lba + 1, dev->disk_block_size);
            usb_dev_idx++;
        }
    }
}
static void detect_xhci_controller(uint8_t bus, uint8_t slot, uint8_t func) {
    if (g_xhci_count >= USB_MAX_XHCI_CONTROLLERS) return;

    uint16_t command = pci_read16(bus, slot, func, 0x04);
    command |= (1u << 1) | (1u << 2);
    pci_write16(bus, slot, func, 0x04, command);

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if ((bar0 & 1u) != 0) return;

    uint64_t mmio_base = (uint64_t)(bar0 & ~0x0Fu);
    if ((bar0 & 0x6u) == 0x4u) {
        uint32_t bar1 = pci_read32(bus, slot, func, 0x14);
        mmio_base |= (uint64_t)bar1 << 32;
    }
    if (mmio_base == 0) return;

    UsbXhciControllerInfo* info = &g_xhci[g_xhci_count];
    XhciRuntimeState* rt = &g_xhci_runtime[g_xhci_count];
    mem_zero(info, sizeof(UsbXhciControllerInfo));
    mem_zero(rt, sizeof(XhciRuntimeState));

    info->present = true;
    info->bus = bus;
    info->slot = slot;
    info->func = func;
    info->mmio_base = mmio_base;

    volatile XhciCapabilityRegs* cap = (volatile XhciCapabilityRegs*)(uintptr_t)mmio_base;
    info->max_slots = (uint8_t)(cap->hcsparams1 & 0xFF);
    info->max_ports = (uint8_t)((cap->hcsparams1 >> 24) & 0xFF);
    if (info->max_ports > USB_MAX_PORTS) info->max_ports = USB_MAX_PORTS;

    xhci_snapshot_ports(cap, info->ports_before_handoff, info->max_ports);
    xhci_bios_handoff(cap);
    info->initialized = xhci_wait_ready(cap);
    xhci_snapshot_ports(cap, info->ports_after_handoff, info->max_ports);

    if (info->initialized && xhci_prepare_runtime(cap, rt, info)) {
        xhci_setup_rings(cap, rt, info);
        xhci_try_enable_slots(cap, rt, info);
    }
    xhci_build_device_list(cap, rt, info);

    g_xhci_count++;
}

void usb_init() {
    g_xhci_count = 0;
    for (uint32_t i = 0; i < USB_MAX_XHCI_CONTROLLERS; i++) {
        mem_zero(&g_xhci[i], sizeof(UsbXhciControllerInfo));
        mem_zero(&g_xhci_runtime[i], sizeof(XhciRuntimeState));
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
                if (class_code == kPciClassSerialBus && subclass == kPciSubclassUsb && prog_if == kPciProgIfXhci) {
                    detect_xhci_controller((uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                    if (g_xhci_count >= USB_MAX_XHCI_CONTROLLERS) break;
                }
            }
        }
    }
    // DODAJ TĘ LINIJKĘ NA KOŃCU:
    usb_register_storage_disks();
}

uint32_t usb_xhci_count() {
    return g_xhci_count;
}

const UsbXhciControllerInfo* usb_get_xhci(uint32_t index) {
    if (index >= g_xhci_count) return nullptr;
    return &g_xhci[index];
}

const char* usb_speed_name(uint8_t speed_id) {
    switch (speed_id) {
    case 1: return "full";
    case 2: return "low";
    case 3: return "high";
    case 4: return "super";
    case 5: return "super+";
    default: return "unknown";
    }
}
