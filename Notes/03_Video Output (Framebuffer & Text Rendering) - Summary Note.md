## The Big Picture

To display anything on screen, you need a **linear framebuffer** — a block of memory where every pixel is stored one after the other. Writing a color value to a memory address = drawing a pixel on screen.

---

## Historical Context (Why Framebuffer)

- **Real mode BIOS routines** — obsolete, only worked in real mode
- **Text mode (0xB800)** — deprecated; screen was a grid of characters, not pixels
- **Linear framebuffer** — the modern standard; UEFI requires it; all boot protocols expose it the same way

---

## Step 1 - Request a Framebuffer (via GRUB/Multiboot2)

Add a framebuffer tag to your multiboot2 header in assembly:

```asm
framebuffer_tag_start:
    dw 0x05   ; type: framebuffer
    dw 0x01   ; optional tag
    dd framebuffer_tag_end - framebuffer_tag_start
    dd 0      ; width  (0 = let bootloader decide)
    dd 0      ; height (0 = let bootloader decide)
    dd 0      ; depth/bpp (0 = let bootloader decide)
framebuffer_tag_end:
```

Setting width/height/depth to 0 lets the bootloader pick the best mode.

---

## Step 2 - Read the Framebuffer Info Tag

GRUB fills in a `framebuffer_info` tag (type = 8) in the multiboot2 info structure. Key fields:

|Field|What it means|
|---|---|
|`framebuffer_addr`|Base address of the framebuffer in physical memory|
|`framebuffer_pitch`|Number of **bytes** per row (not pixels)|
|`framebuffer_width`|Width in pixels|
|`framebuffer_height`|Height in pixels|
|`framebuffer_bpp`|Bits per pixel (usually 32)|
|`framebuffer_type`|0 = indexed color, 1 = RGB direct, 2 = EGA text|

**Pitch** is not the same as width in pixels — it includes any padding bytes at the end of each row.

**Important:** The framebuffer address is **physical**. When you enable paging/virtual memory, map it into virtual memory or you will get a page fault.

---

## Step 3 - Plot a Pixel

To draw a pixel at position (x, y):

```
pixel_address = framebuffer_addr + (y * pitch) + (x * bytes_per_pixel)
```

- `bytes_per_pixel` = `bpp / 8` (for 32bpp = 4 bytes)
- Write your 32-bit color value to that address
- Color format is usually `0x00RRGGBB`

That's the entire drawing model — everything visible on screen goes through this function.

---

## Drawing an Image (Without a Filesystem)

- GIMP can export any image as a C header file (`File → Export As → .h`)
- This gives you a `static char* header_data` array with width, height, and a helper macro `HEADER_PIXEL(data, pixel)`
- Each call to `HEADER_PIXEL` fills a 4-byte array with the next pixel's RGB values
- Use `unsigned char pixel[4]` (not `char`) — signed char causes sign-extension bugs for values above 127

Convert the pixel array to a `uint32_t` color value:

```c
uint32_t color = (uint32_t)pixel[0] << 16 |  // R
                 (uint32_t)pixel[1] << 8  |  // G
                 (uint32_t)pixel[2];          // B
```

Then call your plot pixel function in a nested loop over width and height.

---

## Step 4 - Fonts and Text Rendering

Since framebuffer mode gives you pixels only (no text mode), you must draw text yourself using a font.

### Recommended Font Format: PSF (PC Screen Font v2)

- Simple binary bitmap font format
- Each character (glyph) is stored as a bitmap of `width × height` bits
- Each bit = 1 pixel: `1` = draw foreground color, `0` = draw background color
- Available on Linux at `/usr/share/kbd/consolefonts/`

### Embedding the Font in Your Kernel

Convert the `.psf` file to a linkable ELF object:

```bash
objcopy -O elf64-x86-64 -B i386 -I binary font.psf font.o
```

Link it with your kernel:

```bash
ld -n -o kernel.bin -T linker.ld <other_files> font.o
```

This gives you 3 auto-generated symbols you access in C:

```c
extern char _binary_font_psf_start;
extern char _binary_font_psf_end;
extern char _binary_font_psf_size;
```

---

### PSF Version Detection

Check the first bytes (the "magic number") to determine the version:

- PSF v1 magic: `0x36, 0x04` (2 bytes)
- PSF v2 magic: `0x72, 0xB5, 0x4A, 0x86` (4 bytes)

### PSF v1 Header

- 3 fields: magic (2 bytes), mode (flags), charsize (bytes per glyph)
- Width is always 8, height = charsize, glyph count = 256 (or 512 if mode bit is set)

### PSF v2 Header (32 bytes total)

|Field|Description|
|---|---|
|magic|4-byte magic|
|version|always 0|
|headersize|always 32|
|flags|0 = no unicode table|
|numglyphs|total number of characters|
|bytesperglyph|bytes per glyph|
|height|pixels tall|
|width|pixels wide|

Access the font data:

```c
PSF_font *font = (PSF_font *)&_binary_font_psf_start;
```

---

### Accessing a Glyph

Each character is stored as a bitmap immediately after the header. To get the i-th character:

```c
// PSF v2:
uint8_t* glyph = (uint8_t*)&_binary_font_psf_start
                 + font->headersize
                 + (i * font->bytesperglyph);
```

Each byte in the glyph = one row of 8 pixels. Bit 7 (MSB) = leftmost pixel.

---

### Drawing a Character

Function signature:

```c
void fb_putchar(char symbol, uint16_t x, uint16_t y, uint32_t fg, uint32_t bg);
```

- `x`, `y` are **character** coordinates, not pixel coordinates
- Convert to pixels: `pixel_x = x * font->width`, `pixel_y = y * font->height`
- Loop through each row and each bit of the glyph bitmap
- If the bit is `1`: draw `fg` color at that pixel position; if `0`: draw `bg` color

---

## Summary of the Pipeline

```
Request framebuffer (Multiboot2 tag)
        ↓
Read framebuffer info tag (address, pitch, width, height, bpp)
        ↓
Implement plot_pixel(x, y, color)
        ↓
Embed PSF font via objcopy → link to kernel
        ↓
Parse PSF header → locate glyph bitmaps
        ↓
Implement fb_putchar(char, x, y, fg, bg)
        ↓
Text and images on screen
```

- Reference: [OSDev Framebuffer](https://wiki.osdev.org/Framebuffer), [PSF Font Format](https://www.win.tue.nl/~aeb/linux/kbd/font-formats-1.html), [Multiboot2 Spec](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html)