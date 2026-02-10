TARGET_ARCH = i686-unknown-none-elf
CC = clang --target=$(TARGET_ARCH)
AS = clang --target=$(TARGET_ARCH)
LD = clang --target=$(TARGET_ARCH)
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Wstrict-prototypes -fno-pie
LDFLAGS = -T src/linker.ld -ffreestanding -O2 -nostdlib -static -Wl,--build-id=none
ARCH = i686

SRCDIR = src
BUILDDIR = build
TARGET = dmos.bin

# Rust configuration
RUST_TARGET = rust/i686-unknown-none.json
RUST_TARGET_DIR = i686-unknown-none
RUST_LIB = rust/target/$(RUST_TARGET_DIR)/debug/libmateos_rust.a

SRC_C = $(wildcard $(SRCDIR)/*.c)
SRC_C_ARCH = $(wildcard $(SRCDIR)/arch/$(ARCH)/*.c)
SRC_C_LIBALLOC = $(wildcard $(SRCDIR)/liballoc/*.c)
SRC_S = $(wildcard $(SRCDIR)/*.S)
SRC_S_ARCH = $(wildcard $(SRCDIR)/arch/$(ARCH)/*.S)

OBJ_C = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC_C))
OBJ_C_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.c,$(BUILDDIR)/%.o,$(SRC_C_ARCH))
OBJ_C_LIBALLOC = $(patsubst $(SRCDIR)/liballoc/%.c,$(BUILDDIR)/%.o,$(SRC_C_LIBALLOC))
OBJ_S = $(patsubst $(SRCDIR)/%.S,$(BUILDDIR)/%.o,$(SRC_S))
OBJ_S_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.S,$(BUILDDIR)/%.o,$(SRC_S_ARCH))

# Default target
all: $(TARGET)

# Build Rust library
rust:
	@echo "Building Rust library..."
	@cd rust && cargo +nightly build --target i686-unknown-none.json -Z build-std=core,compiler_builtins -Z build-std-features=compiler-builtins-mem

$(RUST_LIB): rust

$(TARGET): $(OBJ_C) $(OBJ_S) $(OBJ_C_ARCH) $(OBJ_S_ARCH) $(OBJ_C_LIBALLOC) $(RUST_LIB)
	$(LD) $(LDFLAGS) $(OBJ_C) $(OBJ_C_ARCH) $(OBJ_C_LIBALLOC) $(OBJ_S) $(OBJ_S_ARCH) $(RUST_LIB) -o $(TARGET)

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

$(BUILDDIR)/%.o: $(SRCDIR)/liballoc/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)
	rm -rf out.iso
	@cd rust && cargo clean 2>/dev/null || true

test32:
	qemu-system-i386 -display curses -kernel $(TARGET) -initrd initrd.img -no-reboot

test32-gfx:
	qemu-system-i386 -display sdl -vga std -kernel $(TARGET) -initrd initrd.img -no-reboot

test32-vnc:
	@echo "VNC server on :0 (port 5900) - connect with a VNC client"
	qemu-system-i386 -display vnc=:0 -vga std -kernel $(TARGET) -initrd initrd.img -no-reboot

test64:
	qemu-system-x86_64 -display curses -kernel $(TARGET)

stop-test:
	ps aux |grep qemu | awk '{print $2}' | xargs kill

iso:
	@cp $(TARGET) isodir/boot/
	grub-mkrescue -o out.iso isodir

testiso:
	qemu-system-i386 -display curses -cdrom out.iso

.PHONY: clean rust
