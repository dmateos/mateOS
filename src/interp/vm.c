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
  FILE *fp, *new_fp;
  bool write_op = false;
  bool write_param = false;
  char line[1024];
  char *tok;
  uint32_t opcode, parameter;

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

  while (fgets(line, sizeof(line), fp)) {
    char *lptr = line;
    lptr[strlen(lptr) - 1] = '\0';
    write_op = false;
    write_param = false;

    while ((tok = strsep(&lptr, " "))) {
      if (strncmp(tok, "ADD", 3) == 0) {
        opcode = ADD;
        printf("ADD\n");
        if ((tok = strsep(&lptr, " "))) {
          printf("PARAMETER: %s\n", tok);
          parameter = atoi(tok);
          write_op = true;
          write_param = true;
        }
      } else if (strncmp(tok, "SUB", 3) == 0) {
        opcode = SUB;
        printf("SUB\n");
        if ((tok = strsep(&lptr, " "))) {
          printf("PARAMETER: %s\n", tok);
          parameter = atoi(tok);
          write_op = true;
          write_param = true;
        }
      } else if (strncmp(tok, "PRINT", 5) == 0) {
        opcode = PRINT;
        write_op = true;
        printf("PRINT\n");
      } else if (strncmp(tok, "JMP", 3) == 0) {
        opcode = JMP;
        write_op = true;
        printf("JMP\n");
      } else if (strncmp(tok, "PUSH", 4) == 0) {
        opcode = PUSH;
        write_op = true;
        printf("PUSH\n");
      } else if (strncmp(tok, "POP", 3) == 0) {
        opcode = POP;
        write_op = true;
        printf("POP\n");
      } else if (strncmp(tok, "SET", 3) == 0) {
        opcode = SET;
        printf("SET\n");
        if ((tok = strsep(&lptr, " "))) {
          printf("PARAMETER: %s\n", tok);
          parameter = atoi(tok);
          write_op = true;
          write_param = true;
        }
      } else {
        printf("unknown opcode %s\n", tok);
      }
    }

    if (write_op) {
      fwrite(&opcode, sizeof(uint32_t), 1, new_fp);
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