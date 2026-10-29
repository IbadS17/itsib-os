#include "shell.h"

static int string_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static const char *skip_spaces(const char *text)
{
    while (*text == ' ')
        text++;
    return text;
}

static void write_text(const char *text, void (*write_char)(char))
{
    while (*text)
        write_char(*text++);
}

void shell_execute(
    const char *command,
    void (*write_char)(char),
    void (*clear_console)(void)
)
{
    command = skip_spaces(command);

    if (string_equals(command, "help")) {
        write_text("Available commands:\n", write_char);
        write_text("  help  - Show available commands\n", write_char);
        write_text("  about - About ItsIB OS\n", write_char);
        write_text("  echo  - Print text\n", write_char);
        write_text("  clear - Clear console\n", write_char);
    } else if (string_equals(command, "about")) {
        write_text("ItsIB OS - A hobby operating system.\n", write_char);
        write_text("Built with C, Assembly and Limine.\n", write_char);
    } else if (string_equals(command, "clear")) {
        clear_console();
    } else if (command[0] == 'e' &&
               command[1] == 'c' &&
               command[2] == 'h' &&
               command[3] == 'o' &&
               (command[4] == ' ' || command[4] == '\0')) {
        const char *message = skip_spaces(command + 4);
        write_text(message, write_char);
        write_char('\n');
    } else if (*command != '\0') {
        write_text("Unknown command. Type help.\n", write_char);
    }
}
