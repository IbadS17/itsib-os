#ifndef ITSIB_KEYBOARD_H
#define ITSIB_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KEYBOARD_EVENT_CHAR,
    KEYBOARD_EVENT_UP,
    KEYBOARD_EVENT_DOWN
} keyboard_event_type_t;

typedef struct {
    keyboard_event_type_t type;
    char character;
} keyboard_event_t;

void keyboard_init(void);
bool keyboard_get_event(keyboard_event_t *out);

#endif
