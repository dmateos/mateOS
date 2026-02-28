/* Higher-half kernel: linked at VMA 0xC0200000, loaded at LMA 0x200000.
   boot.S sets up paging before jumping to higher-half addresses.
   Multiboot header is within first 8KB of physical image (LMA 0x200000). */
ENTRY(_start)

KERNEL_VIRTUAL_BASE = 0xC0000000;

SECTIONS
{
	. = KERNEL_VIRTUAL_BASE + 0x200000;   /* VMA = 0xC0200000 */

	.text BLOCK(4K) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
	{
		*(.multiboot)
		*(.boot)
		*(.text)
	}

	/* Read-only data. */
	.rodata BLOCK(4K) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
	{
		*(.rodata*)
	}

	/* Read-write data (initialized) */
	.data BLOCK(4K) : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE)
	{
		*(.data)
	}

	/* Read-write data (uninitialized) and stack */
	.bss BLOCK(4K) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
	{
		*(COMMON)
		*(.bss)
	}
}
