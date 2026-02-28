.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */

.set KERNEL_VIRTUAL_BASE, 0xC0000000

/*
 * Multiboot header — placed first in .multiboot (merged into .text by
 * linker).  Must be within first 8KB of the physical image.
 */
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/*
 * Boot page directory and page tables live in BSS (higher-half VMA).
 * We reference them with (symbol - KERNEL_VIRTUAL_BASE) before paging is on.
 * Two page tables identity-map the first 8MB (enough for kernel + BSS).
 * boot_page_dir maps them at both entries 0-1 (identity) and 768-769 (higher-half).
 */
.section .bss
.align 4096
boot_page_dir:
.skip 4096
boot_page_table0:
.skip 4096
boot_page_table1:
.skip 4096

.align 16
stack_bottom:
.skip 16384 /* 16 KiB */
stack_top:

/*
 * Entry point — linked at VMA 0xC02xxxxx, loaded by bootloader at LMA 0x2xxxxx.
 * Runs with paging OFF.  QEMU -kernel translates the VMA entry point to the
 * corresponding LMA for the initial jump (QEMU bug #1044727, fixed 2013).
 * BSS symbols are at VMA (0xC0xxxxxx), so we subtract KERNEL_VIRTUAL_BASE
 * to get their physical addresses before paging is enabled.
 *
 * On entry from bootloader:
 *   EAX = Multiboot magic value (0x2BADB002)
 *   EBX = Pointer to multiboot info structure
 */
.section .boot
.global _start
.type _start, @function
_start:
	cli

	/* Save multiboot regs — we need eax/ebx later but will clobber them */
	mov %eax, %esi        /* esi = multiboot magic */
	mov %ebx, %edi        /* edi = multiboot info pointer */

	/* Use physical address for stack before paging is on */
	mov $(stack_top - KERNEL_VIRTUAL_BASE), %esp

	/*
	 * Build boot_page_table0: identity-map first 4MB (1024 * 4KB pages).
	 * Each PTE = physical_addr | PRESENT | WRITE (0x03).
	 */
	mov $(boot_page_table0 - KERNEL_VIRTUAL_BASE), %ebx
	xor %ecx, %ecx           /* ecx = page index (0..1023) */
1:
	mov %ecx, %edx
	shl $12, %edx             /* edx = page index * 4096 = physical addr */
	or $0x03, %edx            /* present + writable */
	mov %edx, (%ebx,%ecx,4)  /* boot_page_table0[ecx] = edx */
	inc %ecx
	cmp $1024, %ecx
	jl 1b

	/*
	 * Build boot_page_table1: identity-map 4-8MB (pages 1024..2047).
	 * Needed because kernel BSS (page tables) extends past 4MB.
	 */
	mov $(boot_page_table1 - KERNEL_VIRTUAL_BASE), %ebx
	xor %ecx, %ecx           /* ecx = page index (0..1023) */
2:
	mov %ecx, %edx
	add $1024, %edx           /* offset by 1024 pages (4MB) */
	shl $12, %edx             /* edx = (1024 + index) * 4096 = physical addr */
	or $0x03, %edx            /* present + writable */
	mov %edx, (%ebx,%ecx,4)  /* boot_page_table1[ecx] = edx */
	inc %ecx
	cmp $1024, %ecx
	jl 2b

	/*
	 * Set up boot_page_dir:
	 *   Entry 0   -> boot_page_table0 (identity: VA 0-4MB = PA 0-4MB)
	 *   Entry 1   -> boot_page_table1 (identity: VA 4-8MB = PA 4-8MB)
	 *   Entry 768 -> boot_page_table0 (higher-half: VA 0xC0000000+ = PA 0-4MB)
	 *   Entry 769 -> boot_page_table1 (higher-half: VA 0xC0400000+ = PA 4-8MB)
	 */
	mov $(boot_page_dir - KERNEL_VIRTUAL_BASE), %ebx
	mov $(boot_page_table0 - KERNEL_VIRTUAL_BASE), %edx
	or $0x03, %edx            /* present + writable */
	mov %edx, 0(%ebx)         /* entry 0 */
	mov %edx, (768*4)(%ebx)   /* entry 768 */

	mov $(boot_page_table1 - KERNEL_VIRTUAL_BASE), %edx
	or $0x03, %edx            /* present + writable */
	mov %edx, 4(%ebx)         /* entry 1 */
	mov %edx, (769*4)(%ebx)   /* entry 769 */

	/* Load CR3 with physical address of page directory */
	mov $(boot_page_dir - KERNEL_VIRTUAL_BASE), %ecx
	mov %ecx, %cr3

	/* Enable paging: set CR0.PG (bit 31) */
	mov %cr0, %ecx
	or $0x80000000, %ecx
	mov %ecx, %cr0

	/*
	 * Paging is now ON.  EIP is still a physical/identity-mapped address.
	 * Jump to higher-half VMA so we run from 0xC0xxxxxx addresses.
	 */
	lea _start_higher_half, %ecx
	jmp *%ecx

/*
 * Higher-half entry — runs after paging is enabled.
 * Now executing from 0xC0xxxxxx VMA addresses.
 */
.section .text
.global _start_higher_half
_start_higher_half:
	/* Now running at higher-half VMA.  Switch to the virtual-address stack. */
	mov $stack_top, %esp

	/* Push multiboot args for kernel_main(magic, mbi) */
	push %edi    /* multiboot info pointer */
	push %esi    /* multiboot magic */

	call kernel_main

	/* kernel_main should never return, but just in case */
	add $8, %esp
1:	hlt
	jmp 1b
