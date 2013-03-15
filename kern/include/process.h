#ifndef _PROCESS_H_
#define _PROCESS_H_

/*
 * Definition of a process.
 */

/* Get machine-dependent stuff */
#include <machine/pcb.h>

#include "kern/types.h"

#define MAX_PROCESSES 4096

//process structure
struct process {
    pid_t parent_pid;
    pid_t pgrp_id;
    struct cv* exitcv;
    struct lock* exitlock;
    int exited;
    int exitcode;
    struct thread* self;
};

struct pid_table{
	pid_t pid;
        struct process *process;
	struct pid_table *next;
};

void process_bootstrap(void);
pid_t pid_allocate(struct thread* th);
int pid_exists(pid_t pid);
int remove_process(pid_t pid);
struct process* get_process(pid_t pid);

#endif /* _PROCESS_H_ */
