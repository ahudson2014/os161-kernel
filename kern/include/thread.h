#ifndef _THREAD_H_
#define _THREAD_H_

/*
 * Definition of a thread.
 */

/* Get machine-dependent stuff */
#include <machine/pcb.h>
#include "kern/types.h"
#include <machine/trapframe.h>

#define THREAD_PRIORITY_HIGHEST 100
#define THREAD_PRIORITY_LOWEST 0
#define MAX_OPENED_FILES 128 // Arbitrary setting--may need to change

struct addrspace;
struct f_desc;

struct thread {
	/**********************************************************/
	/* Private thread members - internal to the thread system */
	/**********************************************************/
	
	struct pcb t_pcb;
	char *t_name;
	const void *t_sleepaddr;
	char *t_stack;
	
	int t_priority;
	
	/**********************************************************/
	/* Public thread members - can be used by other code      */
	/**********************************************************/
	
	/*
	 * This is public because it isn't part of the thread system,
	 * and will need to be manipulated by the userprog and/or vm
	 * code.
	 */
	struct addrspace *t_vmspace;

	/*
	 * This is public because it isn't part of the thread system,
	 * and is manipulated by the virtual filesystem (VFS) code.
	 */
	struct vnode *t_cwd;
        
        /* 
         * added by rahmanmd
         * 
         * process id allocated to this thread
         * we can find the corresponding process structure of this thread by
         * looking into pid table even after the thread. We can even get the
         * thread data of this thread from the process data structure even 
         * after the thread is destroyed. This way we can keep track of exited
         * or zombie threads 
         */
        pid_t pid; 
        /*
         * we will need parent id while waitpid because we need to check
         * whether it worth to wait on a pid that is not our child. Also
         * we will need to check whether a child really needs to update exit
         * status when its parent left already.
         */
        pid_t ppid;

	// Added by tocurtis
		struct fdesc* t_fdtable[MAX_OPENED_FILES];
};

/* Call once during startup to allocate data structures. */
struct thread *thread_bootstrap(void);

/* Call during panic to stop other threads in their tracks */
void thread_panic(void);

/* Call during shutdown to clean up (must be called by initial thread) */
void thread_shutdown(void);

/*
 * Make a new thread, which will start executing at "func".  The
 * "data" arguments (one pointer, one integer) are passed to the
 * function.  The current thread is used as a prototype for creating
 * the new one. If "ret" is non-null, the thread structure for the new
 * thread is handed back. (Note that using said thread structure from
 * the parent thread should be done only with caution, because in
 * general the child thread might exit at any time.) Returns an error
 * code.
 */
int thread_fork(const char *name, 
		void *data1, unsigned long data2, 
		void (*func)(void *, unsigned long),
		struct thread **ret);

int
aux_thread_fork(const char *name, 
	    void *data1, unsigned long data2,
	    void (*func)(void *, unsigned long),
	    struct thread **ret);

/*
 * Cause the current thread to exit.
 * Interrupts need not be disabled.
 */
void thread_exit(void);

/*
 * Cause the current thread to yield to the next runnable thread, but
 * itself stay runnable.
 * Interrupts need not be disabled.
 */
void thread_yield(void);

/*
 * Cause the current thread to yield to the next runnable thread, and
 * go to sleep until wakeup() is called on the same address. The
 * address is treated as a key and is not interpreted or dereferenced.
 * Interrupts must be disabled.
 */
void thread_sleep(const void *addr);

/*
 * Cause all threads sleeping on the specified address to wake up.
 * Interrupts must be disabled.
 */
void thread_wakeup(const void *addr);

/*
 * Wake up a single thread who is sleeping on "sleep address"
 * ADDR.
 */
void thread_wakeup_single(const void *addr);

/*
 * Set the priority level of a thread
 * 
 */
void thread_set_priority(struct thread *thread, int priority);

/*
 * Get the priority level of a thread
 * 
 */
int thread_get_priority(struct thread *thread);


/*
 * Return nonzero if there are any threads sleeping on the specified
 * address. Meant only for diagnostic purposes.
 */
int thread_hassleepers(const void *addr);


/*
 * Private thread functions.
 */

/* Machine independent entry point for new threads. */
void mi_threadstart(void *data1, unsigned long data2, 
		    void (*func)(void *, unsigned long));

/* Machine dependent context switch. */
void md_switch(struct pcb *old, struct pcb *nu);

int sys_fork1(struct trapframe* tf, int *ret);


#endif /* _THREAD_H_ */
