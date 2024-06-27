// fakedata_module.h

#ifndef FAKEDATA_MODULE_H
#define FAKEDATA_MODULE_H

#include <stdint.h>

#define FAKEDATA_THREAD_STACK_SIZE 4096
#define FAKEDATA_THREAD_PRIORITY 3

extern struct k_thread fakedata_thread_data;
extern k_thread_stack_t fakedata_stack[];

void fakedata_thread(void *arg1, void *arg2, void *arg3);

#endif // FAKEDATA_MODULE_H
