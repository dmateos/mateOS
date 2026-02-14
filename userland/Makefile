CC = gcc
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall
LDFLAGS = -m32 -T user.ld -nostdlib -static -Wl,--build-id=none

PROGRAMS = hello.elf test.elf gui.elf shell.elf winhello.elf winhello_rust.elf winedit.elf winterm.elf winfm.elf wintask.elf ping.elf winsleep.elf httpd.elf cat.elf echo.elf ls.elf tasks.elf ifconfig.elf shutdown.elf touch.elf writefile.elf del.elf cp.elf

all: $(PROGRAMS)

hello.elf: hello.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

test.elf: test.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

gui.elf: gui.o ugfx.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

shell.elf: shell.o syscalls.o cmd_shared.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winhello.elf: winhello.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

RUST_USER_TARGET = ../../rust/i686-unknown-none.json
RUST_USER_LIB = rust-winhello/target/i686-unknown-none/release/libwinhello_rust.a
RUST_USER_SRCS = rust-winhello/Cargo.toml rust-winhello/src/lib.rs

$(RUST_USER_LIB): $(RUST_USER_SRCS)
	cd rust-winhello && cargo +nightly build --release --target $(RUST_USER_TARGET) -Z build-std=core,compiler_builtins -Z build-std-features=compiler-builtins-mem

winhello_rust.elf: $(RUST_USER_LIB) syscalls.o
	$(CC) $(LDFLAGS) -Wl,-s -o $@ $(RUST_USER_LIB) syscalls.o
	@echo "Built $@"

winedit.elf: winedit.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winterm.elf: winterm.o ugfx.o syscalls.o cmd_shared.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winfm.elf: winfm.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

wintask.elf: wintask.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

ping.elf: ping.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winsleep.elf: winsleep.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

httpd.elf: httpd.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

cat.elf: cat.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

echo.elf: echo.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

ls.elf: ls.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

tasks.elf: tasks.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

ifconfig.elf: ifconfig.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

shutdown.elf: shutdown.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

touch.elf: touch.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

writefile.elf: writefile.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

del.elf: del.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

cp.elf: cp.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf

.PHONY: all clean
