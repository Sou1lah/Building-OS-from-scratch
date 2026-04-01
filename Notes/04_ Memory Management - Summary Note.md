## The Big Picture

Memory management has 4 layers that work on top of each other. From lowest to highest:

```
Heap Allocator       ← alloc(5 bytes) — any size
Virtual Memory Mgr   ← manages virtual address space — page-sized
Paging               ← hardware translation virtual ↔ physical
Physical Memory Mgr  ← tracks real RAM — page-sized
```

When you call `malloc(5)`:

1. Heap looks for 5 free bytes in its current pool
2. If none found, asks the VMM for more pages
3. VMM asks the PMM for a free physical page
4. PMM returns a free physical page → VMM maps it → Heap returns your 5-byte address

---

## Layer 1 - Physical Memory Manager (PMM)

Tracks which physical RAM pages (4096 bytes each) are free or in use. Only the kernel uses it directly.

### Bitmap Method (simplest approach)

- One bit per page: `0` = free, `1` = used
- 1 byte tracks 8 pages = 32KB of memory
- Store the bitmap as an array at a known location in memory

**Converting bit position ↔ address:**

- `bit_number = (row * bits_per_row) + column`
- `address = bit_number * PAGE_SIZE` (e.g. bit 3 = address `0x3000`)
- Reverse: `bit_number = address / 4096`, then `row = bit / 8`, `col = bit % 8`

**Three core operations (use bitwise operators):**

- Test if a page is free
- Mark a page as used (set bit)
- Mark a page as free (clear bit)

**PMM responsibilities:**

- At startup: mark all non-usable memory (MMIO, ACPI, bootloader, kernel) as used
    
- Only hand out pages from regions the bootloader marked as free RAM
    
- Reference: [OSDev Physical Memory Manager](https://wiki.osdev.org/Page_Frame_Allocation)
    

---

## Layer 2 - Paging

Paging = the hardware mechanism that translates **virtual addresses** (what your code uses) into **physical addresses** (actual RAM). Done by the CPU's MMU automatically.

### Why it matters

- Your kernel can live at address `0xFFFFFFFF80000000` even though it's physically at 1MB
- Two programs can both "use" address `0x1000` — they each map to different physical pages
- You can protect pages (read-only, no-execute, kernel-only)

### Page Table Structure (x86_64, 4KB pages — 4 levels)

```
CR3 register → PML4 → PDPR → PD → PT → Physical Page
```

- Each table = 4KB, exactly 512 entries, each entry = 8 bytes
- A virtual address is split into 5 fields: PML4 index (9 bits), PDPR index (9 bits), PD index (9 bits), PT index (9 bits), page offset (12 bits)

**Common page entry flags:**

- Bit 0: Present (page exists)
- Bit 1: Writable
- Bit 2: User-accessible (ring 3)
- Bit 63: No-execute (NX)

**For 2MB pages:** skip the PT level. **For 1GB pages:** skip PD and PT.

### Accessing Page Tables (two approaches)

**Recursive Paging:**

- Point one PML4 entry back at PML4 itself (e.g. entry 510)
- Build special virtual addresses using that entry number repeatedly to reach any table
- Works well for a single address space, awkward for multiple

**Direct Map (recommended for 64-bit):**

- Map ALL physical memory at a fixed virtual address offset (e.g. `dmap_base = 0xFFFF800000000000`)
- To access any physical address: `virtual = physical + dmap_base`
- Requires setup once at boot, uses address space but is much simpler and more flexible

### Page Fault (#PF, vector 14)

Fires when address translation fails. Check:

- `CR2` = the virtual address that caused the fault
- Error code bit 0: `0` = page not present, `1` = protection violation
- Error code bit 1: `0` = read, `1` = write
- Error code bit 2: `0` = kernel mode, `1` = user mode

Correct response: map the memory if the program is allowed to have it, or terminate the program if not.

- Reference: [OSDev Paging](https://wiki.osdev.org/Paging), [Intel SDM Vol 3A](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

---

## Layer 3 - Virtual Memory Manager (VMM)

The VMM is the software layer that manages virtual address space. Paging is just the hardware tool it uses.

### What it does

- Tracks which virtual address ranges are in use (via a linked list of **vm_object** structs)
- Maps virtual ranges to physical pages (by programming the page tables)
- One VMM per process/address space; kernel VMM covers the higher half of all address spaces

### VM Object (virtual memory range)

```c
typedef struct vm_object {
    uintptr_t base;       // start virtual address
    size_t length;        // size in bytes (page-aligned)
    size_t flags;         // read/write/exec/mmio
    struct vm_object* next;
} vm_object;
```

**Flags:** `VM_FLAG_WRITE`, `VM_FLAG_EXEC`, `VM_FLAG_USER`, `VM_FLAG_MMIO`

### Key functions

```c
void* vmm_alloc(size_t length, size_t flags, void* arg);
void  vmm_free(void* addr);
```

- `vmm_alloc` walks the vm_object list to find a free virtual range, asks the PMM for a physical page, maps it, returns the virtual address
- For MMIO: pass the known physical address in `arg` + `VM_FLAG_MMIO` — do not allocate a new physical page, just map the given physical address

### Freeing

- Find the vm_object by address
- If normal memory: unmap page tables AND tell PMM the physical pages are free
- If MMIO: only unmap page tables (no PMM involved — it's not real RAM)

### Design decisions to make early

- Single or multiple address spaces? (affects all map/unmap function signatures — add a `page_table_root*` parameter)
    
- Kernel in higher half, user programs in lower half? (common design: split VMMs)
    
- Reference: [OSDev VMM](https://wiki.osdev.org/Memory_management)
    

---

## Layer 4 - Heap Allocator

The heap allocates **any number of bytes** (unlike the VMM which only does page-sized chunks). This is where `malloc`/`free` live. The kernel has its own heap (`kmalloc`/`kfree`).

### Evolution of the Design

**Step 1 — Bump Allocator (simple, no free)**

```c
void* alloc(size_t size) {
    void* addr = cur;
    cur += size;
    return addr;
}
void free(void* ptr) { return; } // does nothing
```

Fast but memory is never reclaimed. Good starting point only.

**Step 2 — Linked List Allocator (real heap)**

Each chunk of memory has a **node header** before it:

```c
typedef struct Heap_Node {
    size_t size;       // bytes this node owns
    bool free;         // is this chunk available?
    struct Heap_Node* next;
    struct Heap_Node* prev;
} Heap_Node;
```

The address returned to the caller = `(address of node) + sizeof(Heap_Node)`.

**alloc(n):**

- Walk the list looking for a free node with `size >= n`
- If none: expand the heap (ask VMM for more pages, add a new node at the end)
- If found: split the node if it's much bigger than needed (see below), mark it used, return address after the node header

**free(ptr):**

- Go back to `ptr - sizeof(Heap_Node)` to find the node
- Mark it free
- Merge with adjacent free neighbors (coalescing) to reduce fragmentation

### Splitting (avoid wasting memory)

When a free node is much larger than requested:

- Cut it into two nodes: one for the allocation, one leftover free node
- Insert the leftover into the linked list
- Don't create nodes smaller than `sizeof(Heap_Node)` (they can never be allocated)
- Use a minimum allocation size (e.g. `0x10` or `0x20` bytes) for alignment

### Merging / Coalescing (reduce fragmentation)

When freeing a node:

- Check if the left neighbor is also free → merge them
- Check if the right neighbor is also free → merge them
- Result: one larger free node instead of two small ones

### Heap Initialization

```c
Heap_Node* heap_start = INITIAL_HEAP_VIRTUAL_ADDRESS;
heap_start->size = INITIAL_HEAP_SIZE; // e.g. 8KB
heap_start->free = true;
heap_start->next = NULL;
heap_start->prev = NULL;
```

- Kernel heap: place right after the kernel binary, rounded up to the next page, leave one unmapped guard page before it (catches bugs)
- User heap: place in the lower half of the address space

### Heap Expansion

When `alloc` reaches the end with no suitable node:

- Ask the VMM to map more pages starting after the last node
- Append a new free node there
- Proceed with split as normal

### Common Pitfalls

- Always `memset` returned memory to zero if C++ constructors expect clean memory (cost: CPU time)
    
- Track whether node sizes include or exclude the `sizeof(Heap_Node)` overhead — be consistent
    
- After merging/splitting, update both `next` and `prev` pointers of affected neighbors
    
- Reference: [OSDev Heap Allocator](https://wiki.osdev.org/Heap), [Slab Allocator](https://wiki.osdev.org/Slab_Allocation)
    

---

## Summary: Who Talks to Who

```
Program calls malloc(5)
    → Heap: find or expand
        → VMM: alloc page-sized virtual range
            → PMM: find free physical page
            → Paging: map phys → virt
        → Heap: split into 5-byte chunk
    → Program receives virtual address
```

All addresses the program ever sees are **virtual**. Only the PMM and paging layer deal with physical addresses.