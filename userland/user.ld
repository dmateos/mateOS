/* Linker script for userland programs */
ENTRY(_start)

SECTIONS
{
    /* Start at 0x700000 (7MB) - within the 8MB identity-mapped region,
       above the heap (0x400000-0x600000) */
    . = 0x00700000;

    .text : {
        *(.text*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata*)
    }

    /* Data section */
    . = ALIGN(4096);
    .data : {
        *(.data*)
    }

    /* BSS (uninitialized data) */
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
