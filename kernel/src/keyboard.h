#ifndef ITSIB_KEYBOARD_H
#define ITSIB_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_init(void);
bool keyboard_getchar(char *out);

#endif
