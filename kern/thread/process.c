#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <array.h>
#include <machine/spl.h>
#include <machine/pcb.h>
#include <process.h>
#include <thread.h>
#include <curthread.h>
#include <scheduler.h>
#include <addrspace.h>
#include <vnode.h>
#include <synch.h>
#include "opt-synchprobs.h"


/* Table of pid */
static struct process *process_table[MAX_PROCESSES+1];

void process_bootstrap(void)
{
    int i=0;
    kprintf("process: process table initialization...\n");
    for(i=1;i<MAX_PROCESSES+1;i++)	
    {
        process_table[i] = NULL;
    }
}

pid_t pid_allocate(struct thread* th)
{
    int i=0;
    pid_t pid = -1;
    for(i=1;i<MAX_PROCESSES+1;i++)	
    {
        if(process_table[i] == NULL)
            break;
    }
    
    //kprintf("process: pid_alloc(): available pid = %d\n", i);
    if(i<=MAX_PROCESSES)
    {
        pid = i;
        char name[16];
        process_table[i] = kmalloc(sizeof(struct process));
        process_table[i]->exitcode = 0;
        snprintf(name, sizeof(name), "cv_thread%d", pid);
        process_table[i]->exitcv = cv_create(name);
        process_table[i]->exited = 0;
        process_table[i]->parent_pid = -1;
        process_table[i]->pgrp_id = -1;        
        snprintf(name, sizeof(name), "lock_thread%d", pid);
        process_table[i]->exitlock = lock_create(name);        
        //kprintf("process pid_alloc: copying selfthread\n");
        process_table[i]->self = kmalloc(sizeof(struct thread));
        memcpy(process_table[i]->self, th, sizeof(struct thread));
        process_table[i]->self->pid = pid;
    }
    //else
    //    panic("Process Table Overflow...");
    
    //kprintf("process pid_alloc: allocated pid: %d\n", pid);
    return pid;
}

int pid_exists (pid_t pid)
{
    if(pid>=1 && pid<=MAX_PROCESSES && process_table[pid] != NULL)
        return 1;
    else return 0;
}

int remove_process(pid_t pid)
{
    if(!pid_exists(pid))
        return -1;
    
    cv_destroy(process_table[pid]->exitcv);
    lock_destroy(process_table[pid]->exitlock);
        
    kfree(process_table[pid]->self);    
    kfree(process_table[pid]);
    process_table[pid] = NULL;          
}

struct process* get_process(pid_t pid)
{    
    return process_table[pid];
}

//void set_ppid(pid_t pid, pid_t ppid)
//{
//    struct process* p = get_process(pid);
//    
//    if(p != NULL)
//    {
//        p->parent_pid = ppid;
//    }
//}