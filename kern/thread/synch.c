/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}

	lock->owner_thread = NULL;
		
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	// add stuff here as needed
	//kfree(lock->owner_thread); //potential bug: we shouldn't destroy thread; this is thread module's job
	kfree(lock->name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	
	int spl;

	// Check to make sure we have an actual lock
	assert(lock != NULL);

	// Go to kernel mode
	spl = splhigh();

	// Sleep until the lock is available
	while (lock->owner_thread != NULL) thread_sleep(lock);

	// Lock is now available
	lock->owner_thread = curthread;

	// Return to user mode
	splx(spl);
	
}

void
lock_release(struct lock *lock)
{
	int spl;

	// Check to make sure we have an actual lock
	assert(lock != NULL);

	spl = splhigh();

	// Make sure the thread calling this owns the lock
	assert(lock->owner_thread == curthread);

	// Set the owner of the lock to NULL
	lock->owner_thread = NULL;

	// Wake up a single thread waiting on the lock
	thread_wakeup_single(lock);

	// Return to user mode
	splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
	// Not sure if I need to go into kernel mode

	// Check to make sure we have an actual lock
	assert(lock != NULL);
	
	if(lock->owner_thread == curthread)
		return 1;   
	else
		return 0;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
	
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	// add stuff here as needed
	
	kfree(cv->name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Go into kernel mode
	// Check that cv and lock are present
	int spl;

	// Check to make sure we have an actual lock
	assert(lock != NULL);

	// Check to make sure we have an actual cv
	assert(cv != NULL);

	spl = splhigh();
	
	// Release the lock
	//if (lock_do_i_hold(lock))
		lock_release(lock);
	
	// Put the thread into the waiting queue of the cv
	thread_sleep(cv);

	// Reacquire the lock
	lock_acquire(lock);

	// Return to user mode
	splx(spl);
	
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int spl;

	// Check to make sure we have an actual lock
	assert(lock != NULL);
	
	// Check to make sure we have an actual cv
	assert(cv != NULL);
	
	spl = splhigh();
	
	// Release the lock
	lock_release(lock);

	// Wake up a single thread waiting on the lock
	thread_wakeup_single(cv);
	
	// Reacquire the lock
	lock_acquire(lock);

	// Return to user mode
	splx(spl);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int spl;

	// Check to make sure we have an actual lock
	assert(lock != NULL);

	// Check to make sure we have an actual cv
	assert(cv != NULL);

	spl = splhigh();
	
	// Release the lock
	lock_release(lock);

	// Wake up all threads waiting on the lock
	thread_wakeup(cv);
	
	// Reacquire the lock
	lock_acquire(lock);

	// Return to user mode
	splx(spl);
}
