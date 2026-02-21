CC = gcc
CFLAGS = -m32 -nostdlib -nostdinc -Iinclude -Ismallerc/include -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall
LDFLAGS = -m32 -T user.ld -nostdlib -static -Wl,--build-id=none

PROGRAMS = hello.elf test.elf cctest.elf gui.elf shell.elf winhello.wlf winhello_rust.wlf winedit.wlf winterm.wlf winfm.wlf wintask.wlf ping.elf winsleep.wlf httpd.elf cat.elf echo.elf ls.elf tasks.elf ifconfig.elf shutdown.elf touch.elf writefile.elf del.elf cp.elf kill.elf burn.elf wintempleos.wlf smallerc.elf as86.elf ld86.elf cc.elf tcc.elf
SMALLERC_CFLAGS = -m32 -nostdlib -nostdinc -Iinclude -Ismallerc/include -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall
TINYCC_CFLAGS = -m32 -nostdlib -nostdinc -Iinclude -Itinycc/vendor -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall -DONE_SOURCE=1

DOOMGENERIC_H = doom/doomgeneric/doomgeneric/doomgeneric.h
DOOM_DIR = doom/doomgeneric/doomgeneric
DOOM_ALL_C = $(wildcard $(DOOM_DIR)/*.c)
DOOM_EXCLUDE_C = \
	$(DOOM_DIR)/doomgeneric_xlib.c \
	$(DOOM_DIR)/doomgeneric_win.c \
	$(DOOM_DIR)/doomgeneric_sdl.c \
	$(DOOM_DIR)/doomgeneric_emscripten.c \
	$(DOOM_DIR)/doomgeneric_linuxvt.c \
	$(DOOM_DIR)/doomgeneric_soso.c \
	$(DOOM_DIR)/doomgeneric_sosox.c \
	$(DOOM_DIR)/doomgeneric_allegro.c \
	$(DOOM_DIR)/i_allegromusic.c \
	$(DOOM_DIR)/i_allegrosound.c \
	$(DOOM_DIR)/i_sdlmusic.c \
	$(DOOM_DIR)/i_sdlsound.c
DOOM_CORE_C = $(filter-out $(DOOM_EXCLUDE_C),$(DOOM_ALL_C))
DOOM_SRC_C = $(DOOM_CORE_C) doom/doomgeneric_mateos.c doom/doom_mateos_start.c doom/compat/compat.c
DOOM_OBJ = $(DOOM_SRC_C:.c=.o)
DOOM_PORT_CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall \
	-Idoom/compat -Idoom -Idoom/doomgeneric/doomgeneric \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	-DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE -DCMAP256

all: $(PROGRAMS) crt0.o cprint.o libtiny.a

hello.elf: hello.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

test.elf: test.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

cctest.elf: cctest.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

gui.elf: gui.o ugfx.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

shell.elf: shell.o syscalls.o cmd_shared.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winhello.wlf: winhello.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

RUST_USER_TARGET = ../../rust/i686-unknown-none.json
RUST_USER_LIB = rust-winhello/target/i686-unknown-none/release/libwinhello_rust.a
RUST_USER_SRCS = rust-winhello/Cargo.toml rust-winhello/src/lib.rs

$(RUST_USER_LIB): $(RUST_USER_SRCS)
	cd rust-winhello && cargo +nightly build --release --target $(RUST_USER_TARGET) -Z build-std=core,compiler_builtins -Z build-std-features=compiler-builtins-mem

winhello_rust.wlf: $(RUST_USER_LIB) syscalls.o
	$(CC) $(LDFLAGS) -Wl,-s -o $@ $(RUST_USER_LIB) syscalls.o
	@echo "Built $@"

winedit.wlf: winedit.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winterm.wlf: winterm.o ugfx.o syscalls.o cmd_shared.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winfm.wlf: winfm.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

wintask.wlf: wintask.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

ping.elf: ping.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

winsleep.wlf: winsleep.o ugfx.o syscalls.o libc.o
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

kill.elf: kill.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

burn.elf: burn.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

wintempleos.wlf: wintempleos.o ugfx.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

smallerc.elf: smallerc_entry.o smallerc/vendor/smlrc.o smallerc/compat_runtime.o syscalls.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

as86.elf: as86.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

ld86.elf: ld86.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

cc.elf: cc.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

tcc.elf: tinycc/tinycc_entry.o tinycc/tcc_main.o tinycc/tcc_builtins.o syscalls.o libc.o
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

crt0.o: crt0.s
	$(CC) $(CFLAGS) -c -o $@ $<

cprint.o: cprint.s
	$(CC) $(CFLAGS) -c -o $@ $<

libtiny.a: libtiny.o
	ar rcs $@ $^

smallerc/vendor/smlrc.o: smallerc/vendor/smlrc.c
	$(CC) $(SMALLERC_CFLAGS) -c -o $@ $<

smallerc/compat_runtime.o: smallerc/compat_runtime.c
	$(CC) $(SMALLERC_CFLAGS) -c -o $@ $<

tinycc/tcc_main.o: tinycc/vendor/tcc.c
	$(CC) $(TINYCC_CFLAGS) -c -o $@ $<

tinycc/tinycc_entry.o: tinycc/tinycc_entry.c
	$(CC) $(CFLAGS) -c -o $@ $<

tinycc/tcc_builtins.o: tinycc/tcc_builtins.c
	$(CC) $(CFLAGS) -c -o $@ $<

ifneq ($(wildcard $(DOOMGENERIC_H)),)
doom.elf: $(DOOM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built $@"

doom/%.o: doom/%.c
	$(CC) $(DOOM_PORT_CFLAGS) -c -o $@ $<

doom/doomgeneric/doomgeneric/%.o: doom/doomgeneric/doomgeneric/%.c
	$(CC) $(DOOM_PORT_CFLAGS) -c -o $@ $<
else
doom.elf:
	@echo "doomgeneric sources not found."
	@echo "Expected at: userland/doom/doomgeneric/doomgeneric/doomgeneric.h and doomgeneric.c"
	@echo "See: userland/doom/README.md"
endif

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *.elf *.wlf smallerc/*.o smallerc/vendor/*.o tinycc/*.o doom/*.o doom/compat/*.o doom/doomgeneric/doomgeneric/*.o

.PHONY: all clean
