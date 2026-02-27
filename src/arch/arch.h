// Architecture abstraction facade.
// Generic kernel code includes this instead of arch/i686/* directly.
// The i686 name should only appear inside src/arch/ and src/drivers/.

#ifndef _ARCH_ARCH_H
#define _ARCH_ARCH_H

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

#endif
