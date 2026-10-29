#ifndef ITSIB_SHELL_H
#define ITSIB_SHELL_H

void shell_execute(
    const char *command,
    void (*write_char)(char),
    void (*clear_console)(void)
);

#endif
