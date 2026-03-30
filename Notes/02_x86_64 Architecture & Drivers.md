## The Big Picture

Before writing real kernel features, you need to understand how the CPU works at a low level — how it manages memory, handles unexpected events, and talks to hardware devices.

---

## Address Spaces

- **Virtual address** = what your code sees (through the MMU/paging)
- **Physical address** = actual location in RAM
- **Port I/O** = a completely separate address space for older devices, accessed with special `in`/`out` CPU instructions (not normal pointers)
- The **higher half** of virtual memory = reserved for the kernel; **lower half** = for user programs
- The "non-canonical hole" is a gap in the middle of the 64-bit address space that the CPU considers invalid — everything above it is "higher half"

---

## Hello World (Serial Output)

Since you have no screen driver yet, you output debug text through the serial port.

- Use `inb`/`outb` assembly instructions to read/write to port `0x3F8` (COM1)
- Initialize the port first by sending a series of configuration bytes
- Then loop through a string and send each character byte by byte
- To print numbers: repeatedly divide by 10, collect the remainders (they give digits in reverse), then reverse them
- In QEMU: add `-serial file:output.log` to save serial output to a file
- Alternative: use port `0xE9` (debug port) — no initialization needed, instant output in emulators
- Reference: [OSDev Serial Ports](https://wiki.osdev.org/Serial_Ports)

---

## Higher Half Kernel

- The kernel is compiled to run at a high virtual address (`0xFFFFFFFF80000000` for 64-bit, the upper 2GB)
- Physically it can be anywhere in RAM — the MMU/paging maps it to that high address
- This keeps kernel memory out of the way of user programs
- Compile flag needed: `-mcmodel=kernel` (for GCC when kernel is in upper 2GB)
- If using Multiboot2, you must set up paging yourself before moving to the higher half
- If using Limine, you start in the higher half already

---

## GDT (Global Descriptor Table)

The GDT is an x86-specific table that tells the CPU about memory regions and privilege levels. In 64-bit (long mode) it mostly just controls what ring (privilege level) code is running in.

**Key concepts:**

- The GDT is an array of 8-byte entries called **descriptors**
- Each descriptor is referenced by a **selector** = its byte offset in the GDT (e.g. first descriptor = `0x8`, second = `0x10`)
- In 64-bit mode, segmentation is basically disabled — what matters is the **privilege level** (ring 0 = kernel, ring 3 = user)

**Minimum GDT you need:**

- `0x00` Null descriptor (required, always zeroed)
- `0x08` Kernel code (ring 0, 64-bit)
- `0x10` Kernel data (ring 0)
- `0x18` User code (ring 3)
- `0x20` User data (ring 3)

**Loading the GDT:**

- Fill a `uint64_t` array with your descriptors
- Create a `GDTR` struct (size + address, must be `packed`)
- Call `lgdt` instruction with the GDTR address
- Reload all segment registers after (`ds`, `es`, `fs`, `gs`, `ss` via `mov`, and `cs` via a far return/`lretq`)

**Special segment registers to know:**

- `CS` = code segment (current privilege level)
    
- `GS` = commonly used for CPU-local storage in kernels
    
- Reference: [Intel SDM Vol 3A Ch 3](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), [OSDev GDT](https://wiki.osdev.org/GDT)
    

---

## Interrupt Handling (IDT)

An **interrupt** stops whatever is running, calls a handler function, then resumes. It is how the CPU signals errors, hardware events, or timer ticks.

**Key concepts:**

- **IDT** (Interrupt Descriptor Table) = array of 256 entries, each pointing to a handler function
- Load it with the `lidt` instruction (similar to GDT loading)
- **Interrupt gate** = disables further interrupts while handler runs (use this by default)
- **Trap gate** = allows interrupts inside the handler
- `cli` = disable interrupts, `sti` = enable them
- **NMI** (Non-Maskable Interrupt) = cannot be disabled, signals critical hardware failure — just panic on it

**IDT entry fields:**

- Address of handler function (split into 3 parts)
- Code selector (use your kernel CS = `0x8`)
- Flags: type (interrupt/trap gate), DPL (who can trigger it via software), present bit

**Reserved vectors 0-31 (CPU exceptions):**

- `#PF` (14) = Page Fault — most common, check `%cr2` for the bad address
- `#GP` (13) = General Protection Fault — usually a bad segment register operation
- `#DF` (8) = Double Fault — something went very wrong, last chance before system reset

**Important trap after `hlt`:** Always put `hlt` inside a loop (`while(true) hlt;`) — after an interrupt returns, execution resumes at the next instruction after `hlt`, which is garbage

**Remapping the PIC:** The legacy PIC sends interrupts on vectors 0-15 by default, which conflicts with CPU exceptions. Remap it to vectors `0x20`/`0x28` or just disable it entirely when using APIC.

- Reference: [OSDev IDT](https://wiki.osdev.org/IDT), [AMD64 Manual](https://www.amd.com/en/support/tech-docs)

---

## ACPI Tables

ACPI is a standard that describes hardware to the OS — power management, IRQ routing, device addresses, etc. You need it to find where the APIC is.

**Reading order:**

1. Get the **RSDP** (Root System Description Pointer) from the bootloader tag
2. RSDP points to either the **RSDT** (32-bit addresses) or **XSDT** (64-bit addresses) — use XSDT if available
3. RSDT/XSDT is an array of pointers to other tables (SDTs), each with a 4-character signature
4. Search through the array for the table you need (e.g. "APIC" = the MADT table)

**Validation:** Sum all bytes of the descriptor — the result's last byte must be `0`, otherwise ignore it.

**Important notes:**

- All ACPI addresses are physical — map them to virtual memory before accessing
- Signatures are NOT null-terminated — don't `printf` them directly or you'll get garbage
- Reference: [ACPI Specification](https://uefi.org/specifications), [OSDev ACPI](https://wiki.osdev.org/ACPI)

---

## APIC (Advanced Programmable Interrupt Controller)

The APIC replaced the old PIC and handles hardware interrupts, especially for multi-core systems.

**Two types:**

- **Local APIC (LAPIC)** = one per CPU core, handles interrupts for that core, contains the per-core timer
- **I/O APIC** = routes hardware device interrupts to the correct CPU cores

**Setup steps:**

1. Disable the legacy PIC (send initialization command bytes to ports `0x20`/`0xA0`)
2. Find the LAPIC base address via MSR `0x1B` (physical address, usually `0xFEE00000`) — map it to virtual memory
3. Enable the LAPIC by setting bit 8 of the Spurious Vector register (offset `0xF0`) — also set a spurious vector IDT entry in the `0xF0–0xFF` range
4. Find the I/O APIC address from the MADT table in ACPI
5. Configure I/O APIC redirection entries to route device interrupts to your chosen CPU core
6. After every interrupt handler, write `0` to the LAPIC EOI register (offset `0xB0`) to signal "done"

**I/O APIC access pattern:**

- Write the register index to `IOREGSEL` (base address)
    
- Read/write the data via `IOWIN` (base + `0x10`)
    
- Reference: [OSDev APIC](https://wiki.osdev.org/APIC), [Intel 82093AA I/O APIC Datasheet](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/ioapic.pdf)
    

---

## Timers

Timers are used for scheduling (preemptive multitasking) and time tracking.

**Four main x86 timers:**

|Timer|Generates Interrupts|Pollable|Notes|
|---|---|---|---|
|PIT|Yes (periodic + one-shot)|Yes|Fixed 1.19MHz clock, use to calibrate others|
|Local APIC Timer|Yes (periodic + one-shot)|Yes|Per-core, low latency, unknown frequency — needs calibration|
|HPET|Yes (one-shot)|Yes (64-bit)|Known frequency from ACPI, better calibration reference than PIT|
|TSC|One-shot only (via MSR)|Yes|Fastest/most precise, tied to CPU clock — use I-TSC (invariant) version|

**Calibration (for LAPIC/TSC whose frequency is unknown):**

1. Stop both timers
2. Set target timer to max value (count-down) or 0 (count-up)
3. Start both, poll the reference timer for ~10ms
4. Stop both, check how many ticks the target timer counted
5. Now you know: X ticks = 10ms

**Recommended abstractions to implement:**

- `polled_sleep()` — spin-wait for a duration
    
- `poll_timer()` — read current timer value
    
- `arm_interrupt_timer()` — fire an interrupt after N milliseconds
    
- Reference: [OSDev PIT](https://wiki.osdev.org/Programmable_Interval_Timer), [OSDev APIC Timer](https://wiki.osdev.org/APIC_timer)
    

---

## PS/2 Keyboard Driver

**How it works:** When a key is pressed/released, the keyboard sends a **scancode** byte (or bytes) to port `0x60`. An interrupt (IRQ1) fires for every byte received.

**Scancode basics:**

- **MAKE code** = key pressed
- **BREAK code** = key released
- Set 1 is the default (PS/2 controller auto-translates set 2 → set 1)
- In set 1: BREAK code = MAKE code + `0x80`
- Some keys send multi-byte scancodes starting with prefix byte `0xE0`

**Setup:**

- IRQ1 = keyboard, corresponds to I/O APIC pin 1 (redirection entry at offset `0x12`/`0x13`)
- Register an IDT entry and handler function, then unmask that entry in the I/O APIC

**Driver components:**

1. **Circular buffer** — store incoming scancodes as `key_event` structs; fixed size, no memory allocation needed
2. **State machine** — track whether you're in normal state or prefix state (`0xE0` received); changes how the next byte is interpreted
3. **Modifier tracking** — store current state of Shift/Ctrl/Alt/Caps inside each `key_event` struct as a bitmask; apps don't need to track it themselves
4. **Translation (scancode → kernel scancode)** — use a lookup table array indexed by scancode; define your own internal scancode enum
5. **Translation (kernel scancode → printable char)** — two lookup arrays (lowercase and uppercase), or a switch statement; check shift/caps lock to pick the right table

**PS/2 ports:**

- `0x60` = data (read scancode here)
    
- `0x64` = command/status
    
- Reference: [OSDev PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard), [OSDev Scancode Set 1](https://wiki.osdev.org/Keyboard_scancodes)