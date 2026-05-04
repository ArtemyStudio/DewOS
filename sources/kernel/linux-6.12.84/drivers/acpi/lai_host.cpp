#include <lai/host.h>
#include <kernel/io.h>
#include <stdint.hpp>

extern "C" {
    void laihost_outb(uint16_t port, uint8_t val)  { outb(port, val); }
    void laihost_outw(uint16_t port, uint16_t val) { outw(port, val); }
    void laihost_outd(uint16_t port, uint32_t val) { outl(port, val); }

    uint8_t  laihost_inb(uint16_t port) { return inb(port); }
    uint16_t laihost_inw(uint16_t port) { return inw(port); }
    uint32_t laihost_ind(uint16_t port) { return inl(port); }

    void *laihost_map(size_t address, size_t count) {
        return (void*)address; 
    }

    void laihost_unmap(void *pointer, size_t count) {}

    void laihost_pci_writeb(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t, uint8_t) {}
    uint8_t laihost_pci_readb(uint16_t, uint8_t, uint8_t, uint8_t, uint16_t) { return 0xFF; }

    void laihost_log(int level, const char *msg) {
        if (level == LAI_DEBUG_LOG) return;
        kprintf("LAI: %s\n", msg);
    }

    void laihost_panic(const char *msg) {
        critical_error(msg, 0x1A1_666);
    }

    void laihost_sleep(uint64_t ms) {
        for(uint64_t i = 0; i < ms * 10000; i++) { __asm__("pause"); }
    }
}