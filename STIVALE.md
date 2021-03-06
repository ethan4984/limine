# stivale boot protocol specification

The stivale boot protocol aims to be a *simple* to implement protocol which
provides the kernel with most of the features one may need in a *modern*
x86_64 context (although 32-bit x86 is also supported).

## General information

In order to have a stivale compliant kernel, one must have a kernel executable
in the `elf64` or `elf32` format and have a `.stivalehdr` section (described below).
Other executable formats are not supported.

stivale will recognise whether the ELF file is 32-bit or 64-bit and load the kernel
into the appropriate CPU mode.

stivale natively supports (only for 64-bit kernels) and encourages higher half kernels.
The kernel can load itself at `0xffffffff80000000` (as defined in the linker script)
and the bootloader will take care of everything, no AT linker script directives needed.

If the kernel loads itself in the lower half, the bootloader will not perform the
higher half relocation.

*Note: In order to maintain compatibility with Limine and other stivale-compliant*
*bootloaders it is strongly advised never to load the kernel or any of its*
*sections below the 1 MiB physical memory mark. This may work with some stivale*
*loaders, but it WILL NOT work with Limine and it's explicitly discouraged.*

## Kernel entry machine state

### 64-bit kernel

`rip` will be the entry point as defined in the ELF file, unless the `entry_point`
field in the stivale header is set to a non-0 value, in which case, it is set to
the value of `entry_point`.

At entry, the bootloader will have setup paging mappings as such:

```
 Base Physical Address -                    Size                    ->  Virtual address
  0x0000000000000000   - 4 GiB plus any additional memory map entry -> 0x0000000000000000
  0x0000000000000000   - 4 GiB plus any additional memory map entry -> 0xffff800000000000 (4-level paging only)
  0x0000000000000000   - 4 GiB plus any additional memory map entry -> 0xff00000000000000 (5-level paging only)
  0x0000000000000000   -                 0x80000000                 -> 0xffffffff80000000
```

If the kernel is dynamic and not statically linked, the bootloader will relocate it.
Furthermore if bit 2 of the flags field in the stivale header is set, the bootloader
will perform kernel address space layout randomisation (KASLR).

The kernel should NOT modify the bootloader page tables, and it should only use them
to bootstrap its own virtual memory manager and its own page tables.

At entry all segment registers are loaded as 64 bit code/data segments, limits and
bases are ignored since this is Long Mode.

DO NOT reload segment registers or rely on the provided GDT. The kernel MUST load
its own GDT as soon as possible and not rely on the bootloader's.

The IDT is in an undefined state. Kernel must load its own.

IF flag, VM flag, and direction flag are cleared on entry. Other flags undefined.

PG is enabled (`cr0`), PE is enabled (`cr0`), PAE is enabled (`cr4`),
LME is enabled (`EFER`).
If stivale header flag bit 1 is set, then, if available, 5-level paging is enabled
(LA57 bit in `cr4`).

The A20 gate is enabled.

PIC/APIC IRQs are all masked.

`rsp` is set to the requested stack as per stivale header. If the requested value is
non-null, an invalid return address of 0 is pushed to the stack before jumping
to the kernel.

`rdi` will point to the stivale structure (described below).

All other general purpose registers are set to 0.

### 32-bit kernel

`eip` will be the entry point as defined in the ELF file, unless the `entry_point`
field in the stivale header is set to a non-0 value, in which case, it is set to
the value of `entry_point`.

At entry all segment registers are loaded as 32 bit code/data segments.
All segment bases are `0x00000000` and all limits are `0xffffffff`.

DO NOT reload segment registers or rely on the provided GDT. The kernel MUST load
its own GDT as soon as possible and not rely on the bootloader's.

The IDT is in an undefined state. Kernel must load its own.

IF flag, VM flag, and direction flag are cleared on entry. Other flags undefined.

PE is enabled (`cr0`).

The A20 gate is enabled.

PIC/APIC IRQs are all masked.

`esp` is set to the requested stack as per stivale header. An invalid return address
of 0 is pushed to the stack before jumping to the kernel.

A pointer to the stivale structure (described below) is pushed onto this stack
before the entry point is called.

All other general purpose registers are set to 0.

## Bootloader-reserved memory

In order for stivale to function, it needs to reserve memory areas for either internal
usage (such as page tables, GDT, SMP), or for kernel interfacing (such as returned
structures).

stivale ensures that none of these areas are found in any of the sections
marked as "usable" in the memory map.

The location of these areas may vary and it is implementation specific;
these areas may be in any non-usable memory map section, or in unmarked memory.

The OS must make sure to be done consuming bootloader information and services
before switching to its own address space, as unmarked memory areas in use by
the bootloader may become unavailable.

Once the OS is done needing the bootloader, memory map areas marked as "bootloader
reclaimable" may be used as usable memory. These areas are not guaranteed to be
aligned, but they are guaranteed to not overlap other sections of the memory map.

## stivale header (.stivalehdr)

The kernel executable shall have a section `.stivalehdr` which will contain
the header that the bootloader will parse.

Said header looks like this:
```c
struct stivale_header {
    uint64_t stack;   // This is the stack address which will be in ESP/RSP
                      // when the kernel is loaded.
                      // It can only be set to NULL for 64-bit kernels. 32-bit
                      // kernels are mandated to provide a vaild stack.
                      // 64-bit and 32-bit valid stacks must be at least 256 bytes
                      // in usable space and must be 16 byte aligned addresses.

    uint16_t flags;   // Flags
                      // bit 0  0 = text mode, 1 = graphics framebuffer mode
                      // bit 1  0 = 4-level paging, 1 = use 5-level paging (if
                                                        available)
                                Ignored if booting a 32-bit kernel.
                      // bit 2  0 = Disable KASLR, 1 = enable KASLR (up to 1GB slide)
                                Ignored if booting a 32-bit or non-relocatable kernel
                      // All other bits undefined.

    uint16_t framebuffer_width;   // These 3 values are parsed if a graphics mode
    uint16_t framebuffer_height;  // is requested. If all values are set to 0
    uint16_t framebuffer_bpp;     // then the bootloader will pick the best possible
                                  // video mode automatically (recommended).
    uint64_t entry_point;      // If not 0, this field will be jumped to at entry
                               // instead of the ELF entry point.
} __attribute__((packed));
```

## stivale structure

The stivale structure returned by the bootloader looks like this:
```c
struct stivale_struct {
    uint64_t cmdline;               // Pointer to a null-terminated cmdline
    uint64_t memory_map_addr;       // Pointer to the memory map (entries described below)
    uint64_t memory_map_entries;    // Count of memory map entries
    uint64_t framebuffer_addr;      // Address of the framebuffer and related info
    uint16_t framebuffer_pitch;
    uint16_t framebuffer_width;
    uint16_t framebuffer_height;
    uint16_t framebuffer_bpp;
    uint64_t rsdp;                  // Pointer to the ACPI RSDP structure
    uint64_t module_count;          // Count of modules that stivale loaded according to config
    uint64_t modules;               // Pointer to the first entry in the linked list of modules (described below)
    uint64_t epoch;                 // UNIX epoch at boot, read from system RTC
    uint64_t flags;                 // Flags
                                    // bit 0: 1 if booted with BIOS, 0 if booted with UEFI
                                    // All other bits undefined.
} __attribute__((packed));
```

## Memory map entry

```c
struct mmap_entry {
    uint64_t base;      // Base of the memory section
    uint64_t length;    // Length of the section
    uint32_t type;      // Type (described below)
    uint32_t unused;
} __attribute__((packed));
```

`type` is an enumeration that can have the following values:

```
1      - Usable RAM
2      - Reserved
3      - ACPI reclaimable
4      - ACPI NVS
5      - Bad memory
10     - Kernel/Modules
0x1000 - Bootloader Reclaimable
```

All other values are undefined.

The kernel and modules loaded **are not** marked as usable memory. They are marked
as Kernel/Modules (type 10).

Usable RAM chunks are guaranteed to be 4096 byte aligned for both base and length.

The entries are guaranteed to be sorted by base address, lowest to highest.

Usable RAM chunks are guaranteed not to overlap with any other entry.

To the contrary, all non-usable RAM chunks are not guaranteed any alignment, nor
is it guaranteed that they do not overlap each other (except usable RAM).

## Modules

The `modules` variable points to the first entry of the linked list of module
structures.
A module structure looks like this:
```c
struct stivale_module {
    uint64_t begin;         // Address where the module is loaded
    uint64_t end;           // End address of the module
    char     string[128];   // String passed to the module (by config file)
    uint64_t next;          // Pointer to the next module (if any), check module_count
                            // in the stivale_struct
} __attribute__((packed));
```
