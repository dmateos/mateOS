#include "gdt.h"
#include "lib.h"

// Pointer to GDT for later modification (TSS)
static gdt_entry_t *gdt_entries = NULL;
static gdt_ptr_t *gdt_pointer = NULL;

static void print_gdt(gdt_entry_t *gdt, int segment, char *name) {
    printf("%s\n", name);
    printf("\tbase_low: 0x%x", gdt[segment].base_low);
    printf("\tbase_middle: 0x%x", gdt[segment].base_middle);
    printf("\tbase_high: 0x%x\n", gdt[segment].base_high);
    printf("\tlimit_low: 0x%x", gdt[segment].limit_low);
    printf("\taccess: 0x%x", gdt[segment].access);
    printf("\tgranularity: 0x%x\n", gdt[segment].granularity);
}

// Helper to set a GDT entry
static void gdt_set_entry(gdt_entry_t *gdt, int num, uint32_t base,
                          uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);

    gdt[num].access = access;
}

static void init_gdt_table(gdt_entry_t *gdt) {
    // Flat memory model
    // GDT Layout:
    //   0x00: Null
    //   0x08: Kernel Code (Ring 0)
    //   0x10: Kernel Data (Ring 0)
    //   0x18: User Code (Ring 3)
    //   0x20: User Data (Ring 3)
    //   0x28: TSS (set later by tss_init)

    // Null segment (index 0)
    gdt_set_entry(gdt, 0, 0, 0, 0, 0);

    // Kernel code segment (index 1, selector 0x08)
    // Access: Present(1), DPL=0, Type=1(code/data), Executable(1),
    // Direction(0), RW(1), Accessed(0) = 1 00 1 1010 = 0x9A Granularity: 4KB
    // blocks, 32-bit protected mode = 0xCF
    gdt_set_entry(gdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Kernel data segment (index 2, selector 0x10)
    // Access: Present(1), DPL=0, Type=1(code/data), Executable(0),
    // Direction(0), RW(1), Accessed(0) = 1 00 1 0010 = 0x92
    gdt_set_entry(gdt, 2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // User code segment (index 3, selector 0x18, RPL=3 -> 0x1B)
    // Access: Present(1), DPL=3, Type=1(code/data), Executable(1),
    // Direction(0), RW(1), Accessed(0) = 1 11 1 1010 = 0xFA
    gdt_set_entry(gdt, 3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // User data segment (index 4, selector 0x20, RPL=3 -> 0x23)
    // Access: Present(1), DPL=3, Type=1(code/data), Executable(0),
    // Direction(0), RW(1), Accessed(0) = 1 11 1 0010 = 0xF2
    gdt_set_entry(gdt, 4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // TSS segment (index 5, selector 0x28) - set later by gdt_set_tss
    gdt_set_entry(gdt, 5, 0, 0, 0, 0);
}

void init_gdt(gdt_ptr_t *gp_ptr, gdt_entry_t *gdt) {
    printf("GDT initializing for i686\n");

    // Save pointers for later TSS setup
    gdt_entries = gdt;
    gdt_pointer = gp_ptr;

    gp_ptr->limit = (sizeof(gdt_entry_t) * GDT_ENTRY_COUNT) - 1;
    gp_ptr->base = (uint32_t)gdt;

    init_gdt_table(gdt);
    flush_gdt(gp_ptr);

    print_gdt(gdt, 0, "Null segment");
    print_gdt(gdt, 1, "Kernel code segment");
    print_gdt(gdt, 2, "Kernel data segment");
    print_gdt(gdt, 3, "User code segment");
    print_gdt(gdt, 4, "User data segment");
    printf("GDT initialized at address 0x%x with %d entries\n", gdt,
           GDT_ENTRY_COUNT);
}

// Set up TSS descriptor in GDT
void gdt_set_tss(uint32_t base, uint32_t limit) {
    if (!gdt_entries) {
        printf("ERROR: GDT not initialized before TSS setup\n");
        return;
    }

    // TSS descriptor (index 5, selector 0x28)
    // Access: Present(1), DPL=0, Type=0(system), TSS 32-bit available = 0x89
    // Granularity: byte granularity, 32-bit = 0x40
    gdt_entries[5].base_low = base & 0xFFFF;
    gdt_entries[5].base_middle = (base >> 16) & 0xFF;
    gdt_entries[5].base_high = (base >> 24) & 0xFF;

    gdt_entries[5].limit_low = limit & 0xFFFF;
    gdt_entries[5].granularity = ((limit >> 16) & 0x0F) | 0x40;

    // 0x89 = Present, DPL=0, System segment, 32-bit TSS available
    gdt_entries[5].access = 0x89;

    printf("TSS descriptor set at GDT index 5 (selector 0x28)\n");
}
