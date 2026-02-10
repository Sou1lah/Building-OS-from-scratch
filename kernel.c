#include <stdint.h> // Include standard integer types (e.g., uint32_t)

/* Multiboot header */
__attribute__((section(".multiboot"))) // Place the following variable in the ".multiboot" section
const uint32_t multiboot_header[] = {
    0x1BADB002,     // magic number required by Multiboot specification
    0x00000000,     // flags (set to 0, meaning no special features requested)
    -(0x1BADB002)   // checksum so that magic + flags + checksum == 0
};

void kernel_main(void) { // Entry point for the kernel
    const char* msg = "HELLO FROM KERNEL"; // Message to display on the screen
    volatile char* vga = (volatile char*)0xB8000; // Pointer to VGA text buffer memory

    for (int i = 0; msg[i]; i++) { // Loop through each character in the message
        vga[i * 2] = msg[i];           // Write character to VGA buffer (character byte)
        vga[i * 2 + 1] = 0x0F;         // Set attribute byte (white text on black background)
    }

    for (;;); // Infinite loop to prevent kernel from exiting
}
