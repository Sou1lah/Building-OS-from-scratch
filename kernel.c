#include <stdint.h>
#include <stddef.h> // Standard integer types and size definitions 

/* =========================================================
   MULTIBOOT HEADER
   Required so GRUB recognizes and loads the kernel.
   ========================================================= */

__attribute__((section(".multiboot")))
const uint32_t multiboot_header[] = {
    0x1BADB002,          // magic number
    0x0,                 // flags
    -(0x1BADB002)        // checksum
};


/* =========================================================
   VGA TERMINAL DEFINITIONS
   ========================================================= */

// VGA text buffer physical address (memory-mapped I/O)
#define VGA_MEMORY_ADDRESS 0xB8000

// Screen dimensions in text mode
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

// Current cursor position
static size_t terminal_row;
static size_t terminal_column;

// Current text color
static uint8_t terminal_color;

// Pointer to VGA memory
static volatile uint16_t* terminal_buffer;

/* =========================================================
   Keyboard input handling
   =========================================================
  */

static inline uint8_t inb(uint16_t port) { //inb is used to read a byte from an I/O port. This is essential for handling keyboard input, as the keyboard controller sends data to the CPU through specific I/O ports (e.g., 0x60 for data and 0x64 for status). The inb function allows us to read the scancode of the key that was pressed or released, which we can then process to determine which key it corresponds to and how to handle it in our kernel.
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "d"(port));
    return result; // we return either the scancode of the key that was pressed or released
} // bascilly, this function uses inline assembly to execute the 'inb' instruction, which reads a byte from the specified I/O port. The result is stored in the variable 'result', which is then returned to the caller. This allows us to interact with hardware devices like the keyboard by reading data from their associated I/O ports.

static inline void outb(uint16_t port, uint8_t value) { //outb is used to write a byte to an I/O port. This is important for sending commands to the keyboard controller, such as acknowledging that we've read a scancode or configuring the controller's behavior. By using outb, we can control how the keyboard interacts with our kernel and ensure that we properly handle input events.
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); // ALREADY DEFINED ABOVE, IGNORE THIS
} // this function uses inline assembly to execute the 'outb' instruction, which writes a byte (value) to the specified I/O port. This allows us to send commands or data to hardware devices like the keyboard by writing to their associated I/O ports.

uint8_t keyboard_read_scancode() {
    return inb(0x60); // Read scancode from keyboard data port
} // this function simply calls the inb function with the keyboard data port (0x60) to read the scancode of the key that was pressed or released. The scancode can then be processed to determine which key event occurred.

char scancode_to_ascii(uint8_t scancode) {
    // This is a very basic mapping for demonstration purposes.
    // A real implementation would need to handle more keys and modifiers.
    switch (scancode) {
        case 0x1E: return 'A';
        case 0x30: return 'B';
        case 0x2E: return 'C';
        case 0x20: return 'D';
        case 0x12: return 'E';
        case 0x21: return 'F';
        case 0x22: return 'G';
        case 0x23: return 'H';
        case 0x17: return 'I';
        case 0x24: return 'J';
        case 0x25: return 'K';
        case 0x26: return 'L';
        case 0x32: return 'M';
        case 0x31: return 'N';
        case 0x18: return 'O';
        case 0x19: return 'P';
        case 0x10: return 'Q';
        case 0x13: return 'R';
        case 0x1F: return 'S';
        case 0x14: return 'T';
        case 0x16: return 'U';
        case 0x2F: return 'V';
        case 0x11: return 'W';
        case 0x2D: return 'X';
        case 0x15: return 'Y';
        case 0x2C: return 'Z';
        default:   return '?'; // Unknown scancode
    }
} // this function takes a scancode as input and returns the corresponding ASCII character. It uses a simple switch statement to map specific scancodes to their ASCII equivalents. In a real implementation, you would want to handle more keys (including numbers, symbols, and modifiers like Shift) and possibly use a more efficient data structure for mapping scancodes to characters.


/* =========================================================
   HELPER FUNCTIONS (INTERNAL)
   ========================================================= */

// Create VGA color byte from foreground and background
// Returns 8-bit color value
static uint8_t vga_entry_color(uint8_t fg, uint8_t bg){ // uint8_t is an unsigned 8-bit integer type, which can hold values from 0 to 255. In VGA text mode, colors are represented as a single byte where the lower 4 bits represent the foreground color and the upper 4 bits represent the background color. The function takes two parameters: fg (foreground color) and bg (background color), both of which are expected to be in the range of 0-15 (since VGA supports 16 colors). The function combines these two values into a single byte by placing the foreground color in the lower 4 bits and the background color in the upper 4 bits using bitwise operations.
    return fg | (bg << 4); // Foreground in lower 4 bits, background in upper 4 bits . Eg. fg=0x7 (light gray), bg=0x0 (black) → 0x07
}

// Combine character + color into 16-bit VGA entry
static uint16_t vga_entry(unsigned char uc, uint8_t color){
    return (uint16_t) uc | ((uint16_t) color << 8); // Character in lower 8 bits, color in upper 8 bits. Eg. 'A' (0x41) with color 0x07 → 0x0741
}

/* Write a byte to an I/O port */
// static inline void outb(uint16_t port, uint8_t value) { // ALREADY DEFINED ABOVE, IGNORE THIS
//     __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
// } // this is used to send commands to the VGA controller to update the hardware cursor position. The outb function writes a byte (value) to a specified I/O port (port). In this case, we use it to communicate with the VGA controller ports (0x3D4 and 0x3D5) to set the cursor position on the screen. The first call tells the VGA controller which byte of the cursor position we are setting (high or low), and the second call sends the actual byte of the position.
// Move hardware cursor (optional advanced feature)
static void terminal_update_cursor(){
    // Calculate cursor position in VGA memory
    uint16_t pos = terminal_row * VGA_WIDTH + terminal_column;

    // Send commands to VGA controller ports (0x3D4 and 0x3D5)
    outb(0x3D4, 14); // Tell VGA we are setting the high byte of the cursor position
    outb(0x3D5, pos >> 8); // Send the high byte of the position
    outb(0x3D4, 15); // Tell VGA we are setting the low byte of the cursor position
    outb(0x3D5, pos & 0xFF); // Send the low byte of the position
}

// Scroll screen up when reaching bottom
static void terminal_scroll(){
    // Move all lines up by one
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[(y - 1) * VGA_WIDTH + x] = terminal_buffer[y * VGA_WIDTH + x];
        }
    }
    // Clear the last line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
    // Move cursor up
    if (terminal_row > 0) {
        terminal_row--;
    }
}



/* =========================================================
   TERMINAL CORE FUNCTIONS (PUBLIC API)
   ========================================================= */

// Initialize terminal:
// - Set default color
// - Reset cursor position
// - Clear screen
void terminal_initialize(void){
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(0x7, 0x0); // Light gray on black
    terminal_buffer = (uint16_t*) VGA_MEMORY_ADDRESS;

    // Clear the screen
    for (size_t y = 0; y < VGA_HEIGHT; y++) { // this clear screen loop is needed because we are writing directly to VGA memory, so we need to initialize it to a known state (blank spaces with default color). If we didn't do this, the screen might show garbage data from memory or previous boot stages. By filling the entire buffer with spaces, we ensure a clean slate for our terminal output. This is especially important since we're bypassing any BIOS or firmware initialization that might have set up the screen for us.
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

// Clear entire screen
void terminal_clear(void){
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    terminal_update_cursor();
 } // how this function work is by iterating through every position on the screen (80 columns x 30 rows) and writing a space character with the current color to each position in the VGA memory buffer. This effectively clears the screen by overwriting any existing characters with blank spaces. After clearing, it resets the cursor position to the top-left corner (0,0) and updates the hardware cursor accordingly.

// Set current text color
void terminal_setcolor(uint8_t color){
    terminal_color = color;
}

// Put single character at current cursor position
// Handle:
//   - newline '\n'
//   - wrapping at end of line
//   - scrolling if needed
void terminal_putchar(char c){
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else {
        terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(c, terminal_color);
        terminal_column++;
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }
    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }
    terminal_update_cursor();
} // how this function work is by first checking if the character to be printed is a newline ('\n'). If it is, it moves the cursor to the beginning of the next line. If it's not a newline, it writes the character to the current cursor position in the VGA memory buffer with the current color. After writing, it advances the cursor column. If the column exceeds the screen width, it wraps to the next line. If the row exceeds the screen height, it calls terminal_scroll() to scroll the screen up. Finally, it updates the hardware cursor to reflect the new position.

// Write null-terminated string
void terminal_write(const char* data){ // null terminalted string is a string that ends with a null character '\0' to indicate the end of the string. This function works by iterating through each character in the string until it reaches the null terminator. For each character, it calls terminal_putchar() to print it on the screen. This allows us to easily print strings without needing to specify their length, as the null terminator serves as a marker for the end of the string.
    size_t i = 0;
    while (data[i] != '\0') {
        terminal_putchar(data[i]);
        i++;
    }

}

// Write string with explicit size
void terminal_write_n(const char* data, size_t size){
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
 } // this function works by taking a pointer to a character array (string) and a size parameter that specifies how many characters to write. It iterates through the string up to the specified size and calls terminal_putchar() for each character, allowing us to print a portion of a string or a string that may not be null-terminated.



/* =========================================================
   KERNEL ENTRY POINT
   ========================================================= */

void kernel_main(void)
{

    char c;

    // Initialize terminal subsystem
    terminal_initialize();
    // After this, NEVER write directly to 0xB8000 again    
    // Print welcome message using terminal_write()
    terminal_write("Welcome to MyOS!\n");

    // Infinite loop to keep kernel alive
    
    while (1) {
        // Read scancode from keyboard
        uint8_t scancode = keyboard_read_scancode();
        // Convert scancode to ASCII character
        if (scancode & 0x80) {
            // Key release event, ignore for now
            continue;
        }
        c = scancode_to_ascii(scancode);
        // Print the character to the terminal
        terminal_putchar(c);
    }
}
