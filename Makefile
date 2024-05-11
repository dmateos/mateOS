CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-gcc
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Wstrict-prototypes
LDFLAGS = -T src/linker.ld -ffreestanding -O2 -nostdlib -lgcc
ARCH = i686

SRCDIR = src
BUILDDIR = build
TARGET = dmos.bin

SRC_C = $(wildcard $(SRCDIR)/*.c)
SRC_C_ARCH = $(wildcard $(SRCDIR)/arch/$(ARCH)/*.c)
SRC_S = $(wildcard $(SRCDIR)/*.S)
SRC_S_ARCH = $(wildcard $(SRCDIR)/arch/$(ARCH)/*.S)

OBJ_C = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC_C))
OBJ_C_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.c,$(BUILDDIR)/%.o,$(SRC_C_ARCH))
OBJ_S = $(patsubst $(SRCDIR)/%.S,$(BUILDDIR)/%.o,$(SRC_S))
OBJ_S_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.S,$(BUILDDIR)/%.o,$(SRC_S_ARCH))

$(TARGET): $(OBJ_C) $(OBJ_S) $(OBJ_C_ARCH) $(OBJ_S_ARCH)
	$(LD) $(LDFLAGS) $(OBJ_C) $(OBJ_C_ARCH) $(OBJ_S) $(OBJ_S_ARCH) -o $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/arch/$(ARCH)/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.S
	@mkdir -p $(BUILDDIR)
	$(AS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/arch/$(ARCH)/%.S
	@mkdir -p $(BUILDDIR)
	$(AS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)
	rm -rf out.iso

test32:
	qemu-system-i386 -display curses -kernel $(TARGET) -no-reboot

test64:
	qemu-system-x86_64 -display curses -kernel $(TARGET)

stop-test:
	ps aux |grep qemu | awk '{print $2}' | xargs kill

iso:
	@cp $(TARGET) isodir/boot/
	grub-mkrescue -o out.iso isodir

testiso:
	qemu-system-i386 -display curses -cdrom out.iso

.PHONY: clean
