#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_SIZE 1024
#define STACK_SIZE 1024

enum INSTRUCTIONS {
  ADD = 0x01,
  SUB,
  PRINT,
  JMP,
  PUSH,
  POP,
  SET,
};

typedef struct {
  uint32_t *program_data, *stack;
  uint32_t *ip, *sp;
  uint32_t reg0;
} vm_t;

void assemble_file(const char *file) {
  char opcode[32];
  int parameter;
  FILE *fp, *new_fp;
  uint32_t opcode_b;
  bool write_op = false;
  bool write_param = false;

  fp = fopen(file, "r");
  if (!fp) {
    printf("could not open file %s\n", file);
    return;
  }

  new_fp = fopen("output", "wb");
  if (!new_fp) {
    printf("could not open file output\n");
    fclose(fp);
    return;
  }

  printf("Assembler:\n");
  while (fscanf(fp, "%s %d\n", opcode, &parameter) != EOF) {
    printf("opcode: %s, parameter: %d\n", opcode, parameter);
    if (strcmp(opcode, "ADD") == 0) {
      opcode_b = ADD;
      write_op = true;
      write_param = true;
    } else if (strcmp(opcode, "SUB") == 0) {
      opcode_b = SUB;
      write_op = true;
      write_param = true;
    } else if (strcmp(opcode, "PRINT") == 0) {
      opcode_b = PRINT;
      write_op = true;
      write_param = false;
    } else if (strcmp(opcode, "JMP") == 0) {
      opcode_b = JMP;
      write_op = true;
      write_param = true;
    } else if (strcmp(opcode, "PUSH") == 0) {
      opcode_b = PUSH;
      write_op = true;
      write_param = false;
    } else if (strcmp(opcode, "POP") == 0) {
      opcode_b = POP;
      write_op = true;
      write_param = false;
    } else if (strcmp(opcode, "SET") == 0) {
      opcode_b = SET;
      write_op = true;
      write_param = true;
    } else {
      printf("unknown opcode\n");
    }

    if (write_op) {
      fwrite(&opcode_b, sizeof(uint32_t), 1, new_fp);
    }

    if (write_param) {
      fwrite(&parameter, sizeof(uint32_t), 1, new_fp);
      write_op = false;
    }
  }

  fclose(fp);
  fclose(new_fp);
}

void print_vm_state(vm_t *vm) {
  printf("\nVM State:\n");
  printf("reg0: %d\n", vm->reg0);
  printf("ip: %p\n", vm->ip);
  printf("*ip: %d\n", *vm->ip);
  printf("sp: %p\n", vm->sp);
  printf("*sp: %d\n\n", *vm->sp);
  return;
}

void run_vm(vm_t *vm) {
  printf("Running:\n");
  while (true) {
    switch (*vm->ip) {
    case ADD:
      vm->ip++;
      vm->reg0 += *vm->ip;
      vm->ip++;
      break;
    case SUB:
      vm->ip++;
      vm->reg0 -= *vm->ip;
      vm->ip++;
      break;
    case PRINT:
      vm->ip++;
      printf("%d\n", vm->reg0);
      break;
    case JMP:
      break;
    case PUSH:
      *vm->sp = vm->reg0;
      vm->sp++;
      vm->ip++;
      break;
    case POP:
      vm->sp--;
      vm->reg0 = *vm->sp;
      vm->ip++;
      break;
    case SET:
      vm->ip++;
      vm->reg0 = *vm->ip;
      vm->ip++;
      break;
    }
  }
}

void init_vm(vm_t *vm, const char *file) {
  FILE *fp = fopen(file, "rb");
  uint32_t length;

  if (!fp) {
    printf("could not open file %s\n", file);
    return;
  }

  memset(vm, 0, sizeof(vm_t));

  vm->program_data = malloc(PROGRAM_SIZE * sizeof(uint32_t));
  memset(vm->program_data, 0, PROGRAM_SIZE * sizeof(uint32_t));
  vm->ip = vm->program_data;

  vm->stack = malloc(sizeof(uint32_t) * STACK_SIZE);
  memset(vm->stack, 0, sizeof(uint32_t) * STACK_SIZE);
  vm->sp = vm->stack;

  fseek(fp, 0, SEEK_END);
  length = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  fread(vm->program_data, sizeof(uint32_t), length / sizeof(uint32_t), fp);
  fclose(fp);
}

void free_vm(vm_t *vm) {
  free(vm->program_data);
  return;
}

int main() {
  vm_t vm;

  assemble_file("test.s");
  init_vm(&vm, "output");
  print_vm_state(&vm);
  run_vm(&vm);
  free_vm(&vm);
  return 0;
}