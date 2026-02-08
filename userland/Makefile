CC = gcc
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall
LDFLAGS = -m32 -T user.ld -nostdlib -static -Wl,--build-id=none

PROGRAMS = hello.elf test.elf gui.elf

all: $(PROGRAMS)

hello.elf: hello.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

test.elf: test.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

gui.elf: gui.o ugfx.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf

.PHONY: all clean
