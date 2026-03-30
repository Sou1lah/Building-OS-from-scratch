## The Big Picture

Building an OS is not like building a normal program. You need to:

- Compile your code with special settings
- Link everything together with custom rules
- Package it into a bootable disk image

---

## Step 1 - Freestanding Environment

- Normal programs secretly use OS libraries behind the scenes (like the C standard library)
- Your kernel cannot do that — it IS the OS, so nothing else exists yet
- This is called a **freestanding** environment — no hidden dependencies, you start from scratch
- You can still use a small set of safe headers like `stdint.h` and `stddef.h`

---

## Step 2 - Cross Compiler

- A cross compiler lets you build code for a different machine than the one you're working on
- Two main options: **GCC** (needs a separate build per architecture) or **Clang** (one install, works everywhere via `--target=`)
- Clang is easier to set up — just add `--target=x86_64-elf` to your commands
- Prebuilt GCC toolchains available at [bootlin.com](https://toolchains.bootlin.com)

**Basic compile commands:**

```sh
gcc hello.c -c -o hello.o -ffreestanding
ld hello.o -o hello.elf -nostdlib
```

---

## Step 3 - Makefile (Build Automation)

A Makefile automates all your compile and link commands so you don't run them manually every time.

**Key concepts:**

- Define your tools (`CC`, `LD`) and source files (`C_SRCS`) as variables at the top
- Use `patsubst` to auto-convert source file lists into object file lists — no manual duplication
- Use `$(shell find -name "*.c")` to auto-find all source files
- `.PHONY` targets (like `all` and `clean`) are commands, not files — always run when called
- `@` before a command hides the command itself from output
- `-` before a command tells make to continue even if it fails (useful for `rm`)

**Built-in automatic variables:**

- `$@` = the current target name
- `$<` = the first dependency
- `$^` = all dependencies
- `$(@D)` = directory part of the target path

**For large projects**, makefiles can call other makefiles recursively — one root makefile sets shared settings (toolchain, flags), then calls sub-makefiles for each module (kernel, libraries, apps).

- Reference: [GNU Make Manual](https://www.gnu.org/software/make/manual/make.html)

---

## Step 4 - Linker Script

The linker script tells the linker exactly how to arrange your compiled code in the final binary file.

**4 main parts:**

- **Options** — general settings (entry point, output format)
- **Memory** — describes available RAM (usually skipped for x86)
- **Program Headers** — tells the loader which parts of the file to load into memory
- **Sections** — controls exactly where each piece of code/data lands

**LMA vs VMA — the most important concept:**

- **VMA** (Virtual Memory Address) = where the code _expects_ to be when running
- **LMA** (Load Memory Address) = where the code is actually _placed_ on disk/in memory at boot
- Usually these are the same, but for a higher-half kernel they differ — the kernel is loaded low in physical memory but linked to run at a high address

**The dot operator (`.`):**

- Represents "current address" while the linker is placing sections
- You set it at the start: `. = 0xFFFFFFFF80000000;` to place your kernel in the upper 2GB

**Program header permissions (flags):**

- Bit 0 = execute, Bit 1 = write, Bit 2 = read
- `.text` = read + execute, `.rodata` = read only, `.data` = read + write

**You can define symbols in the linker script** (like `TEXT_BEGIN = .;`) and access them as variables in your C code — useful for knowing where sections start/end at runtime.

- Reference: [GNU ld Manual](https://sourceware.org/binutils/docs/ld/), [LLD Manual](https://lld.llvm.org)

---

## Step 5 - Boot Protocols

A boot protocol is the "handshake" between the bootloader and your kernel. It defines what state the CPU is in when your kernel starts, and what info the bootloader gives you (memory map, display info, etc.).

**Why use a bootloader at all?**

- Real PC hardware is inconsistent and messy
- A bootloader (like GRUB or Limine) handles all the hardware quirks for you
- It gives your kernel a clean, predictable starting state

### Multiboot 2 (used with GRUB)

- Starts the kernel in **32-bit protected mode** — you must manually write assembly code to switch to 64-bit
- Passes info to the kernel as a linked list of tags (memory map, framebuffer, etc.)
- Widely supported — any machine with GRUB can run a Multiboot 2 kernel
- Watch out: the memory map may not mark bootloader memory as "used" — always sanity-check it
- Spec: [Multiboot 2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html)

### Limine Protocol (used with Limine bootloader)

- Starts the kernel already in **64-bit long mode** with paging enabled — much less setup work
- You declare "requests" as C variables with special attributes, and the bootloader fills in the responses
- Supports advanced features: 5-level paging, multi-core boot, KASLR, UEFI
- Easier and more modern than Multiboot 2 for new projects
- Spec: [Limine Protocol](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md)

---

## Step 6 - Generating a Bootable ISO

After building your kernel, you need to package it into an ISO image to run it in an emulator or burn to a USB drive.

### With GRUB:

- Create a folder `disk/boot/grub/` and put `grub.cfg` and your kernel inside
- Run: `grub-mkrescue -o my_iso.iso disk/`
- `grub.cfg` example:

```
menuentry "My OS" {
    multiboot2 /boot/kernel.elf
    boot
}
```

### With Limine:

- Clone the limine binary repo, copy `limine.sys`, `limine-cd.bin`, `limine-cd-efi.bin` into `disk/limine/`
- Add your `limine.conf` and kernel to the disk folder
- Run xorriso to build the ISO, then run `limine bios-install` to make it BIOS-bootable
- `limine.conf` example:

```
/My OS
    protocol: limine
    kernel_path: boot():/boot/kernel.elf
```

- Tool: [xorriso](https://www.gnu.org/software/xorriso/), [Limine repo](https://github.com/limine-bootloader/limine)

---

## Emulator Tips (QEMU)

- `-S` pauses before running — attach a debugger first
- `-s` opens a GDB server on port 1234
- `-no-reboot` stops the VM on a crash instead of rebooting — essential for debugging
- Compile with `-g` to include debug symbols in your kernel
- Reference: [QEMU Documentation](https://www.qemu.org/docs/master/)