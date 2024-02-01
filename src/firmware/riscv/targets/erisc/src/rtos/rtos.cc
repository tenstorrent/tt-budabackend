#include <stddef.h>
#include "rtos.h"

typedef struct TaskInfo {
    volatile uint32_t *Sp;
    uint8_t Priority;
    uint8_t Active;
    uint8_t Padding[2];
} TaskInfo_t;

TaskInfo_t TaskList[RTOS_MAX_TASKS];
uint32_t ReadyTasks;
uint32_t RunningTask;
TaskInfo_t *pCurrentTask = NULL;

void __attribute__((section(".misc_init"))) RtosScheduler(void)
{
    uint32_t next_task_index;
    ReadyTasks |= (0x1 << RunningTask);
    for (int i = 1; i <= RTOS_MAX_TASKS; i++)
    {
        next_task_index = (RunningTask + i) & (RTOS_MAX_TASKS - 1);
        if (TaskList[next_task_index].Active)
        {
            RunningTask = next_task_index;
            pCurrentTask = &TaskList[next_task_index];
            ReadyTasks &= ~(0x1 << next_task_index);
            break;
        }
    }
}

void __attribute__((section(".misc_init"))) TaskExitTrap (void)
{
    while (1)
    {
        //Yield here.
        //Also set a scratch flag to signal that a Task returned, which should never have happened.
    }
}

void __attribute__((section(".misc_init"))) rtosAddTask( void * task_func, uint32_t stack_ptr, uint8_t task_index, uint8_t priority)
{

    if (task_index == MAIN_TASK_INDEX)
    {   
        //This is for the main task which already has a stack.
        TaskList[task_index].Active = 1;
        TaskList[task_index].Priority = priority;
        pCurrentTask = &TaskList[task_index];
        RunningTask = task_index;
    }
    else
    {
        //Add a task and initialize Task's initial context.
        volatile uint32_t * sp = (uint32_t *)(stack_ptr - TASK_CONTEXT_SIZE);

        TaskList[task_index].Active = 1;
        TaskList[task_index].Priority = priority;
        TaskList[task_index].Sp = sp;
        ReadyTasks |= 0x1 << task_index;

        //Initialize Task context.
        sp[0] = (uint32_t) task_func;
        for (int i = 1; i < TASK_CONTEXT_COUNT; i++)
        {
            sp[i] = 0;
        }
    }
}
