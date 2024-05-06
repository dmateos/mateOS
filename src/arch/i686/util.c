#include "util.h"
#include "../../lib.h"

void print_registers(void) {
  uint32_t eax, ebx, ecx, edx;

  asm volatile("mov %%eax, %0" : "=r"(eax));
  asm volatile("mov %%ebx, %0" : "=r"(ebx));
  asm volatile("mov %%ecx, %0" : "=r"(ecx));
  asm volatile("mov %%edx, %0" : "=r"(edx));

  printf("EAX: 0x%x\n", eax);
  printf("EBX: 0x%x\n", ebx);
  printf("ECX: 0x%x\n", ecx);
  printf("EDX: 0x%x\n", edx);
}

// prob doesnt work
void print_stack(int entries) {
  uint32_t *ebp;

  asm volatile("mov %%ebp, %0" : "=r"(ebp));

  printf("Stack trace:\n");
  for (int i = 0; i < entries; i++) {
    printf("0x%x\n", ebp[i]);
  }
}
