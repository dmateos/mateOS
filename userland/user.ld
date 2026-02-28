/* Linker script for userland programs */
ENTRY(_start)

SECTIONS
{
    /* Start at 0x400000 (4MB) — standard ELF base address for user programs */
    . = 0x00400000;

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
