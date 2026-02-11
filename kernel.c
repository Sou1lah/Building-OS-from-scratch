#include <stdint.h>

__attribute__((section(".multiboot")))
const uint32_t multiboot_header[] = {
    0x1BADB002,
    0x0,
    -(0x1BADB002)
};

volatile uint32_t uptime_seconds = 0; // Track OS uptime in seconds

void update_clock() {
    char buffer[20];
    int index = 0;

    // Convert uptime_seconds to a string
    uint32_t time = uptime_seconds;
    do {
        buffer[index++] = '0' + (time % 10);
        time /= 10;
    } while (time > 0);

    // Write "Uptime: " to VGA buffer below the hello message
    volatile char* vga = (volatile char*)0xB8000;
    const char* prefix = "Uptime: ";
    int i = 0;

    // Start writing on the second line (row 1)
    int row = 1;
    int col = 0;
    int offset = (row * 80 + col) * 2;

    while (prefix[i]) {
        vga[offset + i * 2] = prefix[i];
        vga[offset + i * 2 + 1] = 0x0F; // White text
        i++;
    }

    // Write the time string in reverse order
    for (int j = index - 1; j >= 0; j--) {
        vga[offset + i * 2] = buffer[j];
        vga[offset + i * 2 + 1] = 0x0F; // White text
        i++;
    }

    // Add " s" for seconds
    vga[offset + i * 2] = ' ';
    vga[offset + i * 2 + 1] = 0x0F;
    vga[offset + (i + 1) * 2] = 's';
    vga[offset + (i + 1) * 2 + 1] = 0x0F;
}

void kernel_main(void) {
    const char* msg = "HELLO TO WAEL";
    volatile char* vga = (volatile char*)0xB8000; // VGA text mode buffer

    // Write the hello message on the first line (row 0)
    for (int i = 0; msg[i]; i++) {
        vga[i * 2] = msg[i];
        vga[i * 2 + 1] = 0x0F; // White text
    }

    // Infinite loop to update the clock every second
    while (1) {
        update_clock();
        uptime_seconds++;

        // Simple delay loop (not accurate, but works for demonstration)
        for (volatile uint32_t i = 0; i < 100000000; i++);
    }
}
