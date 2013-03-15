/*
 * hungrybirds.c
 *
 * 	Spawns a mama and some babies
 *
 * This tests concurrent read access to the console driver.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <thread.h>
#include <synch.h>
#include <lib.h>

#define MAXBABIES 10
#define MAXSLEEP 10 // Longest a baby will sleep before eating again
#define MAXFOOD 7 // Maximum amount of food in bowl

static struct lock *bowl_lock;
static struct cv *mama;

volatile int food_in_bowl = 7;
int retval;

void mama_bird(void);
void baby_bird(void);

int
hungrybirds()
{
		
	int	n; // number of baby birds
	
	kprintf("Making mama bird...\n");
	// Create the mama condition variable
	mama = cv_create("mamacv");
	// Create the bowl lock
	bowl_lock = lock_create("bowl");
	
	// Create the mama bird
	kprintf("Forking mama bird...\n");
	thread_fork("Mama_thread", 0, 0, &mama_bird, NULL);	
	
	// Put some food in the bowl
	int f = random() % MAXFOOD;
	// Have at least some reasonable amount of starting food
	if (f < 3)
		f = 3;

	// Create a random number of baby birds
	n = random() % MAXBABIES;
	if (n < 2)	
		n = 2; // At least have a couple of babies!


	kprintf("Making %d babies...\n", n);
	int i;
	for (i = 0; i < n; i++)
	{
		kprintf("Made a baby bird.\n");
		thread_fork("Baby_thread", 0, 0, &baby_bird, NULL);
	}
	
	return 0;
}

void mama_bird(void)
{
		
	int f;

	do {
		// mama is signalled when bowl needs refilled
		kprintf("Mama is waiting on a signal to fill the bowl...\n");		
		lock_acquire(bowl_lock);
		cv_wait(mama, bowl_lock);
		kprintf("\nMama has been signaled\n");
		
		f= random() % MAXFOOD;
		// Have at least some reasonable amount of starting food
		if (f < 3)
			f = 3;
				
		kprintf("Mama is refilling the bowl with %d worms...\n", f);
		food_in_bowl = f; // refill the bowl
		
		kprintf("Mama is releasing the lock...\n\n");
		lock_release(bowl_lock); // Unlock the bowl
		
	} while (1);

}

void baby_bird(void)
{
		
	int sleep_time;

	do {
		// Don't try to eat until mama has filled the bowl
		/* This handles an issue where the bowl lock is released so that 
			mama can refill bowl, but other babies enter scheduler first
			and claim the lock. */
		while (food_in_bowl <= 0);
		kprintf("\nBaby bird acquiring bowl lock...\n");
		lock_acquire(bowl_lock); // Lock out other babies		 
		
		kprintf("Baby bird has acquired the lock.\n");
		kprintf("Baby bird is now eating...\n");
		food_in_bowl--; // eat a portion
		kprintf("%d worms remain.\n", food_in_bowl);

		if (food_in_bowl <=0)
		{
			kprintf("\nOut of food.\n");
						
			kprintf("Signalling mama...\n");
			cv_signal(mama, bowl_lock); // alert mama to refill
			kprintf("I told mama to refill the bowl...\n");
			kprintf("Releasing bowl lock...\n");
			lock_release(bowl_lock); // release the lock
		
		}
		else	
		{	
			kprintf("Baby bird is releasing the lock...\n");
			lock_release(bowl_lock); // Unlock the bowl
		}
		kprintf("Baby bird is sleeping...\n\n");
		sleep_time = random() % MAXSLEEP;
		clocksleep(sleep_time); // Sleep a random number of seconds
	} while (1);
	
}


