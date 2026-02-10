#include <stdint.h>

/* Multiboot header */
__attribute__((section(".multiboot")))
const uint32_t multiboot_header[] = {
    0x1BADB002,     // magic
    0x00000000,     // flags
    -(0x1BADB002)   // checksum
};

void kernel_main(void) {
    const char* msg = "HELLO FROM KERNEL";
    volatile char* vga = (volatile char*)0xB8000;

    for (int i = 0; msg[i]; i++) {
        vga[i * 2] = msg[i];
        vga[i * 2 + 1] = 0x0F;
    }

    for (;;);
}
