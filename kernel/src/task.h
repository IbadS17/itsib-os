#ifndef ITSIB_TASK_H
#define ITSIB_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TASK_MAX_COUNT 8
#define TASK_STACK_SIZE 4000

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_FINISHED
} task_state_t;

typedef void (*task_entry_t)(void);

typedef struct {
    uint64_t id;
    task_state_t state;
    task_entry_t entry;
    void *stack;
    size_t stack_size;
} task_t;

void task_init(void);
int task_create(task_entry_t entry);
size_t task_count(void);
const task_t *task_get(size_t index);

#endif
