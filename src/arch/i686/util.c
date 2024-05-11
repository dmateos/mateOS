#include "util.h"
#include "../../lib.h"

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

  asm volatile("mov %%eax, %0" : "=r"(eax));
  asm volatile("mov %%ebx, %0" : "=r"(ebx));
  asm volatile("mov %%ecx, %0" : "=r"(ecx));
  asm volatile("mov %%edx, %0" : "=r"(edx));

  printf("Registers:\n");
  printf("EAX: 0x%x\n", eax);
  printf("EBX: 0x%x\n", ebx);
  printf("ECX: 0x%x\n", ecx);
  printf("EDX: 0x%x\n", edx);
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
