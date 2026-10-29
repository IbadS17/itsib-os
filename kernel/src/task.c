#include "task.h"
#include "kheap.h"

static task_t tasks[TASK_MAX_COUNT];
static size_t task_total = 0;
static uint64_t next_task_id = 1;

void task_init(void)
{
    for (size_t i = 0; i < TASK_MAX_COUNT; i++) {
        tasks[i].id = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].entry = 0;
        tasks[i].stack = 0;
        tasks[i].stack_size = 0;
    }

    task_total = 0;
    next_task_id = 1;
}

int task_create(task_entry_t entry)
{
    if (!entry || task_total >= TASK_MAX_COUNT) {
        return -1;
    }

    void *stack = kmalloc(TASK_STACK_SIZE);
    if (!stack) {
        return -2;
    }

    size_t slot = 0;
    while (slot < TASK_MAX_COUNT &&
           tasks[slot].state != TASK_UNUSED)
        slot++;

    if (slot == TASK_MAX_COUNT) {
        kfree(stack);
        return -3;
    }

    tasks[slot].id = next_task_id++;
    tasks[slot].state = TASK_READY;
    tasks[slot].entry = entry;
    tasks[slot].stack = stack;
    tasks[slot].stack_size = TASK_STACK_SIZE;

    task_total++;
    return (int)tasks[slot].id;
}

size_t task_count(void)
{
    return task_total;
}

const task_t *task_get(size_t index)
{
    if (index >= TASK_MAX_COUNT ||
        tasks[index].state == TASK_UNUSED)
        return 0;

    return &tasks[index];
}
