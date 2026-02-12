TARGET_ARCH = i686-unknown-none-elf
CC = clang --target=$(TARGET_ARCH)
AS = clang --target=$(TARGET_ARCH)
LD = clang --target=$(TARGET_ARCH)
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Wstrict-prototypes -fno-pie \
         -I$(SRCDIR)/lwip/src/include -I$(SRCDIR)/lwip
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
SRC_C_DRIVERS = $(wildcard $(SRCDIR)/drivers/*.c)
SRC_S = $(wildcard $(SRCDIR)/*.S)
SRC_S_ARCH = $(wildcard $(SRCDIR)/arch/$(ARCH)/*.S)

OBJ_C = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC_C))
OBJ_C_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.c,$(BUILDDIR)/%.o,$(SRC_C_ARCH))
OBJ_C_LIBALLOC = $(patsubst $(SRCDIR)/liballoc/%.c,$(BUILDDIR)/%.o,$(SRC_C_LIBALLOC))
OBJ_C_DRIVERS = $(patsubst $(SRCDIR)/drivers/%.c,$(BUILDDIR)/drivers/%.o,$(SRC_C_DRIVERS))
OBJ_S = $(patsubst $(SRCDIR)/%.S,$(BUILDDIR)/%.o,$(SRC_S))
OBJ_S_ARCH = $(patsubst $(SRCDIR)/arch/$(ARCH)/%.S,$(BUILDDIR)/%.o,$(SRC_S_ARCH))

# lwIP sources
LWIP_DIR = $(SRCDIR)/lwip/src
LWIP_CFLAGS = -std=gnu99 -ffreestanding -O2 -fno-pie \
              -I$(SRCDIR)/lwip/src/include -I$(SRCDIR)/lwip -I$(SRCDIR)/lwip/include -Wno-address
SRC_LWIP_CORE = $(LWIP_DIR)/core/init.c $(LWIP_DIR)/core/def.c \
                $(LWIP_DIR)/core/inet_chksum.c $(LWIP_DIR)/core/ip.c \
                $(LWIP_DIR)/core/mem.c $(LWIP_DIR)/core/memp.c \
                $(LWIP_DIR)/core/netif.c $(LWIP_DIR)/core/pbuf.c \
                $(LWIP_DIR)/core/tcp.c $(LWIP_DIR)/core/tcp_in.c \
                $(LWIP_DIR)/core/tcp_out.c $(LWIP_DIR)/core/timeouts.c \
                $(LWIP_DIR)/core/udp.c $(LWIP_DIR)/core/raw.c
SRC_LWIP_IPV4 = $(LWIP_DIR)/core/ipv4/etharp.c $(LWIP_DIR)/core/ipv4/icmp.c \
                $(LWIP_DIR)/core/ipv4/ip4.c $(LWIP_DIR)/core/ipv4/ip4_frag.c \
                $(LWIP_DIR)/core/ipv4/ip4_addr.c
SRC_LWIP_NETIF = $(LWIP_DIR)/netif/ethernet.c
SRC_LWIP_API = $(LWIP_DIR)/api/err.c
SRC_LWIP = $(SRC_LWIP_CORE) $(SRC_LWIP_IPV4) $(SRC_LWIP_NETIF) $(SRC_LWIP_API)
OBJ_LWIP = $(patsubst $(LWIP_DIR)/%.c,$(BUILDDIR)/lwip/%.o,$(SRC_LWIP))

# Default target
all: $(TARGET)

# Build Rust library
rust:
	@echo "Building Rust library..."
	@cd rust && cargo +nightly build --target i686-unknown-none.json -Z build-std=core,compiler_builtins -Z build-std-features=compiler-builtins-mem

$(RUST_LIB): rust

$(TARGET): $(OBJ_C) $(OBJ_S) $(OBJ_C_ARCH) $(OBJ_S_ARCH) $(OBJ_C_LIBALLOC) $(OBJ_C_DRIVERS) $(OBJ_LWIP) $(RUST_LIB)
	$(LD) $(LDFLAGS) $(OBJ_C) $(OBJ_C_ARCH) $(OBJ_C_LIBALLOC) $(OBJ_C_DRIVERS) $(OBJ_LWIP) $(OBJ_S) $(OBJ_S_ARCH) $(RUST_LIB) -o $(TARGET)

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

$(BUILDDIR)/drivers/%.o: $(SRCDIR)/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/lwip/%.o: $(LWIP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LWIP_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)
	rm -rf out.iso
	@cd rust && cargo clean 2>/dev/null || true

test32:
	qemu-system-i386 -display curses -kernel $(TARGET) -initrd initrd.img -no-reboot

test32-gfx:
	qemu-system-i386 -display sdl -vga std -kernel $(TARGET) -initrd initrd.img -no-reboot

test32-net:
	qemu-system-i386 -display curses -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev user,id=n0 -no-reboot

test32-net-http:
	qemu-system-i386 -display curses -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 -no-reboot

test32-gfx-net:
	qemu-system-i386 -display sdl -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev user,id=n0 -no-reboot

test32-gfx-net-http:
	qemu-system-i386 -display sdl -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 -no-reboot

test32-vnc-net-http:
	@echo "VNC server on :0 (port 5900) - connect with a VNC client"
	qemu-system-i386 -display vnc=:0 -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 -no-reboot

test32-net-tap:
	qemu-system-i386 -display curses -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev tap,id=n0,ifname=tap0,script=no,downscript=no -no-reboot

test32-gfx-net-tap:
	qemu-system-i386 -display sdl -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev tap,id=n0,ifname=tap0,script=no,downscript=no -no-reboot

test32-vnc:
	@echo "VNC server on :0 (port 5900) - connect with a VNC client"
	qemu-system-i386 -display vnc=:0 -vga std -kernel $(TARGET) -initrd initrd.img -no-reboot

test32-vnc-net:
	@echo "VNC server on :0 (port 5900) - connect with a VNC client"
	qemu-system-i386 -display vnc=:0 -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev user,id=n0 -no-reboot

test32-vnc-net-tap:
	@echo "VNC server on :0 (port 5900) - connect with a VNC client"
	qemu-system-i386 -display vnc=:0 -vga std -kernel $(TARGET) -initrd initrd.img \
		-device rtl8139,netdev=n0 -netdev tap,id=n0,ifname=tap0,script=no,downscript=no -no-reboot

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
