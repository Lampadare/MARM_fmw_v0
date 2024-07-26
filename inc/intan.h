// intan.h

#ifndef INTAN_H
#define INTAN_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "../inc/neural_data.h"

#define INTAN_THREAD_STACK_SIZE 8192

extern struct k_thread intan_thread_data;
extern k_thread_stack_t intan_stack[];

void intan_thread(void *arg1, void *arg2, void *arg3);

#endif // INTAN_H
