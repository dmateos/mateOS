#ifndef _CMD_SHARED_H
#define _CMD_SHARED_H

typedef struct {
    void (*print)(const char *s);
    void (*print_num)(int n);
    void (*clear)(void);
    const char *exit_help;
} cmd_io_t;

typedef enum {
    CMD_NOT_BUILTIN = 0,
    CMD_HANDLED = 1,
    CMD_EXIT = 2
} cmd_result_t;

cmd_result_t cmd_try_builtin(const char *line, const cmd_io_t *io);

#endif
