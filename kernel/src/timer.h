#ifndef ITSIB_TIMER_H
#define ITSIB_TIMER_H

#include <stdint.h>
#include <stdbool.h>

void timer_init(void);
uint64_t timer_get_ticks(void);

#endif
