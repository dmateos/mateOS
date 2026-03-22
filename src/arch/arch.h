/*
 * arch.h — top-level architecture facade.
 *
 * Generic kernel code includes ONLY this header (and arch_interface.h,
 * which this pulls in automatically).  Never include arch/i686/* or
 * arch/x86_64/* directly from outside src/arch/.
 *
 * The ARCH macro is set by the Makefile:
 *   -DARCH_I686   for the 32-bit i686 build
 *   -DARCH_X86_64 for the 64-bit x86_64 build (future)
 *
 * Each arch directory exposes:
 *   arch_interface.h - common contract (included below)
 *   arch_impl.c      - concrete implementations
 *   ... internal headers used only within src/arch/<arch>/
 *
 * Subsystems that are genuinely arch-neutral (VFS, networking, etc.) must
 * not appear here.  Hardware drivers that must touch I/O ports or PCI
 * may include arch/i686/io.h or arch/i686/pci.h directly -- that is
 * acceptable because those drivers are inherently x86-specific anyway.
 */

#ifndef _ARCH_ARCH_H
#define _ARCH_ARCH_H

/* The common contract every arch must satisfy. */
#include "arch/arch_interface.h"

#if defined(ARCH_I686)

/*
 * i686 — pull in all i686 internal headers so that code which
 * legitimately needs i686 specifics (drivers, arch_impl.c itself) can
 * still reach them via this single include.
 *
 * Generic kernel code (task.c, syscall.c, …) must only use the symbols
 * declared in arch_interface.h, not the i686-specific ones below.
 */
#include "arch/i686/686init.h"
#include "arch/i686/cpu.h"
#include "arch/i686/gdt.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/legacytty.h"
#include "arch/i686/mouse.h"
#include "arch/i686/paging.h"
#include "arch/i686/pci.h"
#include "arch/i686/timer.h"
#include "arch/i686/tss.h"
#include "arch/i686/util.h"
#include "arch/i686/vga.h"

#elif defined(ARCH_X86_64)

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/x86_64init.h"

#else
#error "Unknown ARCH — define ARCH_I686 or ARCH_X86_64 in CFLAGS"
#endif

#endif /* _ARCH_ARCH_H */
