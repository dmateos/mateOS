#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_SIZE 1024
#define STACK_SIZE 1024
#define MAX_LABELS 1024

enum INSTRUCTIONS {
  ADD = 0x01,
  SUB,
  PRINT,
  JMP,
  PUSH,
  POP,
  SET,
  CALL,
  RET,
  LSB,
};

enum ASSEMLBER_DIRECTIVES {
  LABEL,
};

typedef struct {
  uint32_t *text, *stack;
  uint32_t reg0;
  int32_t ip, sp, sbp;
} vm_t;

typedef struct label_t {
  char name[32];
  uint32_t address;
} label_t;

uint32_t find_label(const label_t *labels, const char *str, uint32_t max) {
  for (int i = 0; i < max; i++) {
    if (strcmp(labels[i].name, str) == 0) {
      return labels[i].address;
    }
  }
  return 0;
}

uint32_t assemble_file(const char *file) {
  FILE *fp, *new_fp;
  bool write_op = false;
  bool write_param = false;
  char line[1024];
  char *tok;
  uint32_t opcode, parameter;
  label_t labels[MAX_LABELS];
  uint32_t label_count = 0;
  uint32_t curr_offset = 0;
  uint32_t start_position = 0;

  fp = fopen(file, "r");
  if (!fp) {
    printf("could not open file %s\n", file);
    return 0;
  }

  new_fp = fopen("output", "wb");
  if (!new_fp) {
    printf("could not open file output\n");
    fclose(fp);
    return 0;
  }

  memset(labels, 0, sizeof(labels));

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
        printf("JMP\n");
        opcode = JMP;
        if ((tok = strsep(&lptr, " "))) {
          printf("PARAMETER: %s\n", tok);
          uint32_t addr = find_label(labels, tok, label_count);
          printf("FOUND LABEL: %d\n", addr);
          parameter = addr;
          write_op = true;
          write_param = true;
        }
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
      } else if (strncmp(tok, "CALL", 4) == 0) {
        opcode = CALL;
        printf("CALL\n");
        if ((tok = strsep(&lptr, " "))) {
          printf("PARAMETER: %s\n", tok);
          uint32_t addr = find_label(labels, tok, label_count);
          printf("FOUND LABEL: %d\n", addr);
          parameter = addr;
          write_op = true;
          write_param = true;
        }
      } else if (strncmp(tok, "RET", 3) == 0) {
        opcode = RET;
        write_op = true;
        printf("RET\n");
      } else if (strncmp(tok, "@LABEL", 6) == 0) {
        if ((tok = strsep(&lptr, " "))) {
          strcpy(labels[label_count].name, tok);
          labels[label_count].address = curr_offset;
          if (strncmp(tok, "start", 5) == 0) {
            start_position = curr_offset;
          }
        }
        printf("pushed new label %s with address 0x%x\n",
               labels[label_count].name, labels[label_count].address);
        label_count++;
      } else {
        // printf("unknown opcode %s\n", tok);
      }
    }

    if (write_op) {
      fwrite(&opcode, sizeof(uint32_t), 1, new_fp);
      curr_offset++;
    }

    if (write_param) {
      fwrite(&parameter, sizeof(uint32_t), 1, new_fp);
      curr_offset++;
    }
  }

  fclose(fp);
  fclose(new_fp);

  return start_position;
}

void print_vm_state(vm_t *vm) {
  printf("\nVM State:\n");
  printf("reg0: %d\n", vm->reg0);
  printf("ip: %d\n", vm->ip);
  printf("sp: %d\n\n", vm->sp);
  printf("sbp: %d\n\n", vm->sbp);
  return;
}

void run_vm(vm_t *vm) {
  printf("Running:\n");
  while (true) {
    switch (vm->text[vm->ip]) {
    case ADD:
      vm->ip++;
      vm->reg0 += vm->text[vm->ip];
      vm->ip++;
      break;
    case SUB:
      vm->ip++;
      vm->reg0 -= vm->text[vm->ip];
      vm->ip++;
      break;
    case PRINT:
      vm->ip++;
      printf("%d\n", vm->reg0);
      break;
    case JMP:
      vm->ip++;
      vm->ip = vm->text[vm->ip];
      break;
    case PUSH:
      vm->stack[vm->sp] = vm->reg0;
      vm->sp++;
      vm->ip++;
      break;
    case POP:
      vm->sp--;
      vm->reg0 = vm->stack[vm->sp];
      vm->ip++;
      break;
    case SET:
      vm->ip++;
      vm->reg0 = vm->text[vm->ip];
      vm->ip++;
      break;
    case CALL:
      vm->ip++;
      vm->sbp = vm->sp;
      vm->stack[vm->sp] = vm->ip + 1;
      vm->sp++;
      vm->ip = vm->text[vm->ip];
      break;
    case RET:
      vm->sp--;
      vm->ip = vm->stack[vm->sp];
      break;
    case LSB:
      vm->ip++;
      vm->reg0 = vm->stack[vm->sbp + vm->text[vm->ip]];
      vm->ip++;
      break;
    }
  }
}

void init_vm(vm_t *vm, const char *file, uint32_t start_offset) {
  FILE *fp = fopen(file, "rb");
  uint32_t length;

  if (!fp) {
    printf("could not open file %s\n", file);
    return;
  }

  memset(vm, 0, sizeof(vm_t));

  vm->text = malloc(PROGRAM_SIZE * sizeof(uint32_t));
  memset(vm->text, 0, PROGRAM_SIZE * sizeof(uint32_t));
  vm->ip = start_offset;

  vm->stack = malloc(sizeof(uint32_t) * STACK_SIZE);
  memset(vm->stack, 0, sizeof(uint32_t) * STACK_SIZE);
  vm->sp = 0;
  vm->sbp = 0;

  fseek(fp, 0, SEEK_END);
  length = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  fread(vm->text, sizeof(uint32_t), length / sizeof(uint32_t), fp);
  fclose(fp);
}

void free_vm(vm_t *vm) {
  free(vm->text);
  free(vm->stack);
  return;
}

int main() {
  vm_t vm;
  uint32_t start_offset;

  if ((start_offset = assemble_file("test.s")) == 0) {
    printf("no start found\n");
    exit(0);
  }
  printf("offset: %d\n", start_offset);
  init_vm(&vm, "output", start_offset);
  print_vm_state(&vm);
  run_vm(&vm);
  free_vm(&vm);
  return 0;
}