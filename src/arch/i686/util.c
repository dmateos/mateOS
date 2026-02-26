#include "util.h"
#include "lib.h"

void cause_div_exception(void) {
  asm volatile("div %0" : : "r"(0));
  return;
}

int check_protected_mode(void) {
  uint32_t cr0;

  asm volatile("mov %%cr0, %0" : "=r"(cr0));

  if (cr0 & 0x1) {
    return 1;
  } else {
    return 0;
  }
}

void print_registers(void) {
  uint32_t eax, ebx, ecx, edx;
  uint32_t esp, ebp, esi, edi;
  uint32_t ds, es, fs, gs;
  uint32_t cs, ss;

  asm volatile("mov %%eax, %0" : "=r"(eax));
  asm volatile("mov %%ebx, %0" : "=r"(ebx));
  asm volatile("mov %%ecx, %0" : "=r"(ecx));
  asm volatile("mov %%edx, %0" : "=r"(edx));

  asm volatile("mov %%esp, %0" : "=r"(esp));
  asm volatile("mov %%ebp, %0" : "=r"(ebp));
  asm volatile("mov %%esi, %0" : "=r"(esi));
  asm volatile("mov %%edi, %0" : "=r"(edi));

  asm volatile("mov %%ds, %0" : "=r"(ds));
  asm volatile("mov %%es, %0" : "=r"(es));
  asm volatile("mov %%fs, %0" : "=r"(fs));
  asm volatile("mov %%gs, %0" : "=r"(gs));

  asm volatile("mov %%cs, %0" : "=r"(cs));
  asm volatile("mov %%ss, %0" : "=r"(ss));

  printf("Registers:\n");
  printf("EAX: 0x%x ", eax);
  printf("EBX: 0x%x ", ebx);
  printf("ECX: 0x%x ", ecx);
  printf("EDX: 0x%x\n", edx);

  printf("ESP: 0x%x ", esp);
  printf("EBP: 0x%x ", ebp);
  printf("ESI: 0x%x ", esi);
  printf("EDI: 0x%x\n", edi);

  printf("DS: 0x%x ", ds);
  printf("ES: 0x%x ", es);
  printf("FS: 0x%x ", fs);
  printf("GS: 0x%x\n", gs);

  printf("CS: 0x%x ", cs);
  printf("SS: 0x%x\n\n", ss);
}

// prob doesnt work
void print_stack(uint32_t entries) {
  uint32_t *ebp;

  asm volatile("mov %%ebp, %0" : "=r"(ebp));

  printf("Stack trace:\n");
  for (uint32_t i = 0; i < entries; i++) {
    printf("0x%x\n", ebp[i]);
  }
}

void cpu_get_info(cpu_info_t *out) {
  if (!out) return;
  uint32_t eax, ebx, ecx, edx;

  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0));

  *(uint32_t *)&out->vendor[0] = ebx;
  *(uint32_t *)&out->vendor[4] = edx;
  *(uint32_t *)&out->vendor[8] = ecx;
  out->vendor[12] = '\0';
  out->max_leaf = eax;

  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(1));

  uint32_t stepping = eax & 0xF;
  uint32_t model = (eax >> 4) & 0xF;
  uint32_t family = (eax >> 8) & 0xF;
  uint32_t ext_model = (eax >> 16) & 0xF;
  uint32_t ext_family = (eax >> 20) & 0xFF;

  uint32_t display_family = family;
  uint32_t display_model = model;
  if (family == 0xF) display_family += ext_family;
  if (family == 0x6 || family == 0xF) display_model += (ext_model << 4);

  out->family = display_family;
  out->model = display_model;
  out->stepping = stepping;
  out->feature_ecx = ecx;
  out->feature_edx = edx;
}

void print_cpu_info(void) {
  cpu_info_t info;
  cpu_get_info(&info);

  kprintf("CPU vendor: %s\n", info.vendor);
  kprintf("CPUID max leaf: 0x%x\n", info.max_leaf);
  kprintf("Family: %d  Model: %d  Stepping: %d\n",
          info.family, info.model, info.stepping);
  kprintf("Feature ECX: 0x%x\n", info.feature_ecx);
  kprintf("Feature EDX: 0x%x\n", info.feature_edx);
}
