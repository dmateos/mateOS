#include "libc.h"
#include "syscalls.h"

static void spawn_service(const char *name) {
    int pid = spawn(name);
    if (pid < 0) {
        print("init: failed to start ");
        print(name);
        print("\n");
        return;
    }
    print("init: started ");
    print(name);
    print(" pid=");
    print_num(pid);
    print("\n");
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("init: boot sequence start\n");

    spawn_service("httpd.elf");

    for (;;) {
        int shell_pid = spawn("shell.elf");
        if (shell_pid < 0) {
            print("init: failed to start shell.elf, retrying\n");
            sleep_ms(500);
            continue;
        }

        print("init: started shell.elf pid=");
        print_num(shell_pid);
        print("\n");

        int code = wait(shell_pid);
        print("init: shell exited code=");
        print_num(code);
        print(", respawning\n");
    }
}
