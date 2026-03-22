/* linker64.ld — x86_64 higher-half kernel
 *
 * VMA 0xC0200000, LMA 0x00200000.  Same offset trick as the 32-bit port
 * (0xC0000000 offset keeps the kernel below 4GB in virtual space, which
 * simplifies the boot page tables and stays compatible with the existing
 * PHYS_TO_KVIRT / KVIRT_TO_PHYS macros in memlayout.h).
 *
 * OUTPUT_FORMAT elf64-x86-64 is needed so the linker emits a 64-bit ELF.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(long_mode_entry)

KERNEL_VIRTUAL_BASE = 0xC0000000;

SECTIONS
{
    . = KERNEL_VIRTUAL_BASE + 0x200000;   /* VMA = 0xC0200000 */

    .text BLOCK(4K) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
    {
        *(.multiboot2)
        *(.boot)
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata BLOCK(4K) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
    {
        *(.rodata*)
    }

    /* Initialized data */
    .data BLOCK(4K) : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE)
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data (zero-filled by bootloader) */
    .bss BLOCK(4K) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
}
