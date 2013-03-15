/*
 * Scheduler.
 *
 * The default scheduler is very simple, just a round-robin run queue.
 * You'll want to improve it.
 */

#include <types.h>
#include <lib.h>
#include <scheduler.h>
#include <thread.h>
#include <machine/spl.h>
#include <queue.h>
//#include <stdlib.h>

/*
 *  Scheduler data
 */

// Queue of runnable threads
static struct queue *runqueue;

// Anti-starvation reset
static int cycles = 0;

// Scheduler type -- see scheduler.h for types
int scheduler_type = SCHEDULER_FIFO;
//int scheduler_type = SCHEDULER_RANDOM;
//int scheduler_type = SCHEDULER_MLFQ;

/*
 * Setup function
 */
void
scheduler_bootstrap(void)
{
	runqueue = q_create(32);
	if (runqueue == NULL) {
		panic("scheduler: Could not create run queue\n");
	}

	// Print the scheduler type
	if(scheduler_type == SCHEDULER_FIFO)
		kprintf("\n\n***Using FIFO Scheduler Algorithm***\n\n");
	if(scheduler_type == SCHEDULER_RANDOM)
		kprintf("\n\n***Using Random Scheduler Algorithm***\n\n");
	if(scheduler_type == SCHEDULER_RANDOM)
		kprintf("\n\n***Using Multi-Level Feedback Queue Scheduler Algorithm***\n\n");
}

/*
 * Ensure space for handling at least NTHREADS threads.
 * This is done only to ensure that make_runnable() does not fail -
 * if you change the scheduler to not require space outside the 
 * thread structure, for instance, this function can reasonably
 * do nothing.
 */
int
scheduler_preallocate(int nthreads)
{
	assert(curspl>0);
	return q_preallocate(runqueue, nthreads);
}

/*
 * This is called during panic shutdown to dispose of threads other
 * than the one invoking panic. We drop them on the floor instead of
 * cleaning them up properly; since we're about to go down it doesn't
 * really matter, and freeing everything might cause further panics.
 */
void
scheduler_killall(void)
{
	assert(curspl>0);
	while (!q_empty(runqueue)) {
		struct thread *t = q_remhead(runqueue);
		kprintf("scheduler: Dropping thread %s.\n", t->t_name);
	}
}

/*
 * Cleanup function.
 *
 * The queue objects to being destroyed if it's got stuff in it.
 * Use scheduler_killall to make sure this is the case. During
 * ordinary shutdown, normally it should be.
 */
void
scheduler_shutdown(void)
{
	scheduler_killall();

	assert(curspl>0);
	q_destroy(runqueue);
	runqueue = NULL;
}

/*
 * Actual scheduler. Returns the next thread to run.  Calls cpu_idle()
 * if there's nothing ready. (Note: cpu_idle must be called in a loop
 * until something's ready - it doesn't know whether the things that
 * wake it up are going to make a thread runnable or not.) 
 */
struct thread *
scheduler(void)
{
	// meant to be called with interrupts off
	assert(curspl>0);
	
	while (q_empty(runqueue)) {
		cpu_idle();
	}

	// You can actually uncomment this to see what the scheduler's
	// doing - even this deep inside thread code, the console
	// still works. However, the amount of text printed is
	// prohibitive.
	// 
	//print_run_queue();
	
	/* We will have a defined variable in scheduler.h that will control
		the type of scheduling */
	
	if(scheduler_type == SCHEDULER_RANDOM)
	{
						
		/* Random queue method */
		// We could manipulate q->next_read, an integer that indexes the next in line
		// i.e. pick an index based on the size of the queue (q->size), and change
		// runqueue->next_read to that index

		// We might also be able to just jump in and get a random index from the queue
				
		// queue size is 32 by default
		int queue_size = q_getsize(runqueue);
		int random_index;

		struct thread * temp_thread;

		// We will have to get the thread number from within the active part
		// of the queue
		int start = q_getstart(runqueue);
		int end = q_getend(runqueue);
		
		
		int random_range = (end - start);
		
		// We have a problem if the start and end are the same
		assert(random_range != 0);

		// The startup code seems to have issues if you pick it right off the bat
		

		if (random_range < 0)
			random_range = random_range + queue_size;
		
		// No need to pick a random thread if there is only 1 in the queue
		if (random_range == 1)
			return q_remhead(runqueue);


		DEBUG(DB_THREADS, "Number of active threads: %u.\n", random_range);
		DEBUG(DB_THREADS, "Start: %u.\n", start);
		DEBUG(DB_THREADS, "End: %u.\n", end);
		random_index = (random() % random_range + start) % queue_size;
								
		DEBUG(DB_THREADS, "%u index chosen.\n", random_index);
		
		// Now, we have to move our chosen thread to the front of the line
		// There is probably some other way to do this that is more efficient, but
		// I had no success with q_getguy()

		// We start with the next thread in the queue, and work our way to the chosen one
		while(q_getstart(runqueue) != random_index)
		{
			temp_thread = q_remhead(runqueue);
			q_addtail(runqueue, temp_thread);
		}
		
		DEBUG(DB_THREADS, "New start: %u.\n", q_getstart(runqueue));
		DEBUG(DB_THREADS, "New end: %u.\n", q_getend(runqueue));

		return q_remhead(runqueue);
		
	}
	
	else if (scheduler_type == SCHEDULER_MLFQ)
	{
		/* MLFQ method */
		
		// We will go through all of our queue, looking for the highest priority thread
		// By starting at the next read and working up, on a tie we are taking the first in
		
		
		// queue size is 32 by default
		int queue_size = q_getsize(runqueue);
		int i;
		int chosen_index;
		int priority;
		int random_choice;

		struct thread * temp_thread;

		// We will have to get the thread number from within the active part
		// of the queue
		int start = q_getstart(runqueue);
		int end = q_getend(runqueue);
		
		cycles++;
		
		if (cycles > 2000) {
		// reset priorities 
			//kprintf("Resetting priorities");
			i = start;
			while( i != end)
			{
				temp_thread = q_getguy(runqueue, i);
				DEBUG(DB_THREADS, "Setting priority\n");
				thread_set_priority(temp_thread, 50);
							
				i = (i+1) % queue_size;
			}
			cycles = 0;
			// A bit of randomness to prevent immediate restarving
			return q_remhead(runqueue);
		}


		int highest_priority = -1; // 100 is maximum priority

		i = start;
		
		while( i != end)
		{
			temp_thread = q_getguy(runqueue, i);
			DEBUG(DB_THREADS, "Getting priority\n");
			priority = thread_get_priority(temp_thread);
			DEBUG(DB_THREADS, "Priority: %u.\n", priority);
			if (priority > highest_priority)
			{
				chosen_index = i;
				highest_priority = priority;
			}
			
			// In the event of a tie, random pick
			else if (priority == highest_priority)
			{
				random_choice == random() % 3;
				if (random_choice == 0)
					chosen_index = i;
			}

			i = (i+1) % queue_size;
		}
				
		DEBUG(DB_THREADS, "Start: %u.\n", start);
		DEBUG(DB_THREADS, "End: %u.\n", end);					
		DEBUG(DB_THREADS, "%u index chosen with priority %u.\n", chosen_index, highest_priority);
		//kprintf("%u index chosen with priority %u.\n", chosen_index, highest_priority);
		// Now, we have to move our chosen thread to the front of the line
		// There is probably some other way to do this that is more efficient, but
		// I had no success with q_getguy()

		// We start with the next thread in the queue, and work our way to the chosen one
		while(q_getstart(runqueue) != chosen_index)
		{
			temp_thread = q_remhead(runqueue);
			q_addtail(runqueue, temp_thread);
		}
		
		DEBUG(DB_THREADS, "New start: %u.\n", q_getstart(runqueue));
		DEBUG(DB_THREADS, "New end: %u.\n", q_getend(runqueue));

		return q_remhead(runqueue);
	}
	
	// Fall through to default FIFO scheduler

	return q_remhead(runqueue);
}

/* 
 * Make a thread runnable.
 * With the base scheduler, just add it to the end of the run queue.
 */
int
make_runnable(struct thread *t)
{
	// meant to be called with interrupts off
	assert(curspl>0);

	return q_addtail(runqueue, t);
}

/*
 * Debugging function to dump the run queue.
 */
void
print_run_queue(void)
{
	/* Turn interrupts off so the whole list prints atomically. */
	int spl = splhigh();

	int i,k=0;
	i = q_getstart(runqueue);
	
	while (i!=q_getend(runqueue)) {
		struct thread *t = q_getguy(runqueue, i);
		kprintf("  %2d: %s %p\n", k, t->t_name, t->t_sleepaddr);
		i=(i+1)%q_getsize(runqueue);
		k++;
	}
	
	splx(spl);
}
