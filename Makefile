CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-gcc
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T src/linker.ld -ffreestanding -O2 -nostdlib -lgcc

SRCDIR = src
BUILDDIR = build
TARGET = dmos.bin

SRC_C = $(wildcard $(SRCDIR)/*.c)
SRC_S = $(wildcard $(SRCDIR)/*.S)
OBJ_C = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC_C))
OBJ_S = $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.o,$(SRC_S))

$(TARGET): $(OBJ_C) $(OBJ_S)
	$(LD) $(LDFLAGS) $(OBJ_C) $(OBJ_S) -o $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.S
	@mkdir -p $(BUILDDIR)
	$(AS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)

test32:
	qemu-system-i386 -display curses -kernel $(TARGET)

test64:
	qemu-system-x86_64 -display curses -kernel $(TARGET)

iso:
	@cp $(TARGET) isodir/boot/
	grub-mkrescue -o out.iso isodir

.PHONY: clean
