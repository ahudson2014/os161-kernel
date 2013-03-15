#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <thread.h>
#include <vm.h>
#include <machine/tlb.h>
#include <curthread.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
************************************************
* sbrk system call and Adress Space Management *
************************************************
 
We will be responsible for defining the as_*** functions and how vm_fault works.
Also how malloc uses sbrk() to get address space.
Addressspace functionalities (as_* functions) are implemented in dumbvm.c.
These will work for dumbvm but to make them working for our vm we need to
modify these functions into a new file (mipsvm.c) such that they can support paging.
NOTE THAT WE ARE ALWAYS LOADING THE ADDRESS SPACE WHENEVR IT IS ACCESSED (READ/WRITE)

as_create():
------------
This funciton allocates space, in the kernel, for a structure that does the bookkeeping
for a single address space. It does NOT allocate space for the stack, the program binary,
etc., just the structure that hold information about the address space.

as_destroy():
-------------
Free all the present pages. Also free the swapped pages latter.
All we need to do in as_destroy() is to call kfree() to destroy the allocation,
it'll in turn call free_kpages() in mipsvm.c which will be responsible to
remove the page from the memory.


as_activate():
--------------
This function activates a given address space as the currently in use one.
Currently this just invalidates the entire TLB on a context switch .
In as_activate, we should shoot down all the tlb entries as we are not
using ASID per process. We need to pass pid in as_copy to work it
properly for fork(). So, we need to change our sys_fork() to cope with
this change.  We need to change all the address management function to
use alloc_pages() for our vm instead of using getppages() in dumbvm.
So, we need to change as_prepare_load() and as_copy() to use
alloc_page() for each of the npages instead of using getppages(npages).

as_define_region():
-------------------
Here we should compare addrspace’s current heap_start and the region end,
and set the heap_start right after (vaddr+size). Make sure to properly align the heap_start
(by page bound).

as_prepare_load():
-------------------
this function is called just before the text or data sections are loaded into memory.
They will just get the required pages for those sections.
That is we should actually allocate each of the required page in this function so that
load_segment() in loadelf.c can properly load the required segment.
Make sure that the allocated segments are zeroed properly. 

as_copy():
-----------
Copy each of the preset pages from the old address space’s page table.
Make sure to copy all the attribute bits (low 12 bits) of the old page table entry.
We also need to copy all the swapped pages beside present pages as well.

as_complete_load(), as_define_stack():
--------------------------------------
default behavior as dumbvm.c

*/


#if OPT_DUMBVM
//do nothing
#else


/*
*    as_create - create a new empty address space. You need to make 
*                sure this gets called in all the right places. You
*                may find you want to change the argument list. May
*                return NULL on out-of-memory error.
*/

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	as->as_heaptop = 0;
	as->as_heapbase = 0;
	as->pid = curthread->pid; 
	

	return as;
}

/*
*    as_copy   - create a new address space that is an exact copy of
*                an old one. Probably calls as_create to get a new
*                empty address space and fill it in, but that's up to
*                you.
*/
int as_copy(struct addrspace *old, struct addrspace **ret, pid_t pid)
{
	struct addrspace *newas;		
	int i;
	
	u_int32_t new_paddr;
	u_int32_t old_vaddr;
	
	//DEBUG(DB_VM, "\nCopying address space...\n");
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}
		
	newas->as_vbase1 = old->as_vbase1;
	newas->as_npages1 = old->as_npages1;
	newas->as_vbase2 = old->as_vbase2;
	newas->as_npages2 = old->as_npages2;
	newas->as_heaptop = newas->as_heapbase = newas->as_vbase2 + (newas->as_npages2*PAGE_SIZE);
	// Note that newas->as_heaptop is equal to old->as_heaptop even with the above calculation

	//DEBUG(DB_VM, "Old address heap top 0%x:\n", old->as_heaptop);	
	//DEBUG(DB_VM, "New address heap top 0%x:\n", newas->as_heaptop);
	
	//DEBUG(DB_VM, "Old address space pid: %d\n", old->pid);
	//DEBUG(DB_VM, "New address space pid: %d\n\n", pid);
	newas->pid = pid;
	if (as_prepare_load(newas)) {
		as_destroy(newas);
		return ENOMEM;
	}
		
	// These should be set up by now
	//assert(newas->as_stackpbase != 0);
	assert(newas->as_heaptop != 0);
		
	// Go through the code segment pages and copy everything over
	for (i=0; i < old->as_npages1; i++)
	{
		//get_ppage(newas->as_vbase1 + (i * PAGE_SIZE), newas->pid);
		//DEBUG(DB_VM, "Grabbed code page.\n");
		
		new_paddr = get_ppage(newas->as_vbase1 + (i * PAGE_SIZE), newas->pid);
		//DEBUG(DB_VM, "New paddr from get_ppage at newas->vbase1: 0x%x\n", new_paddr);
		new_paddr = new_paddr & PAGE_FRAME;
		//DEBUG(DB_VM, "New paddr with page frame: 0x%x\n", new_paddr);
		new_paddr = PADDR_TO_KVADDR(new_paddr);
		//DEBUG(DB_VM, "New paddr after PADDR_TO_KVADDR: 0x%x\n", new_paddr);
		old_vaddr = old->as_vbase1 + (i * PAGE_SIZE);
		//DEBUG(DB_VM, "Old vaddr from old->as_vbase1: 0x%x\n\n", old_vaddr);
		
		memmove((void *)(new_paddr),
			(const void *) (old_vaddr),
			PAGE_SIZE);	
			
		/*
		memmove((void *)PADDR_TO_KVADDR(get_ppage(newas->as_vbase1 + (i * PAGE_SIZE), newas->pid)),
			(const void *) (old->as_vbase1 + (i * PAGE_SIZE)),
			PAGE_SIZE);				
		*/
	}
	
	//DEBUG(DB_VM, "Moving on.\n");
	// Check to see if the code copied correctly
	//DEBUG(DB_VM, "Old address space code at first vaddr: %u\n", old->as_vbase1);
	//DEBUG(DB_VM, "New address space code at first vaddr: %u\n", newas->as_vbase1);

	// Go through the data segment pages and copy everything over
	for (i=0; i < old->as_npages2; i++)
	{
		
		new_paddr = get_ppage(newas->as_vbase2 + (i * PAGE_SIZE), newas->pid);
		//DEBUG(DB_VM, "New paddr from get_ppage at newas->vbase1: 0x%x\n", new_paddr);
		new_paddr = new_paddr & PAGE_FRAME;
		//DEBUG(DB_VM, "New paddr with page frame: 0x%x\n", new_paddr);
		new_paddr = PADDR_TO_KVADDR(new_paddr);
		//DEBUG(DB_VM, "New vaddr after PADDR_TO_KVADDR: 0x%x\n", new_paddr);
		old_vaddr = old->as_vbase2 + (i * PAGE_SIZE);
		//DEBUG(DB_VM, "Old vaddr from old->as_vbase1: 0x%x\n\n", old_vaddr);
		
		memmove((void *)(new_paddr),
			(const void *) (old_vaddr),
			PAGE_SIZE);	
		/*
		memmove((void *)PADDR_TO_KVADDR(get_ppage(newas->as_vbase2 + (i * PAGE_SIZE), newas->pid)),
			(const void *) (old->as_vbase2 + (i * PAGE_SIZE)),
			PAGE_SIZE);
			*/
	}

	//DEBUG(DB_VM, "Moving on.\n");

	// Go through the stack pages and copy everything over
	// Stack goes down from max 
	for (i=0; i < VM_STACKPAGES; i++) 
	{
		new_paddr = get_ppage(USERSTACK - ((i+1) * PAGE_SIZE), newas->pid);
		//DEBUG(DB_VM, "New paddr from get_ppage at stack: 0x%x\n", new_paddr);
		new_paddr = new_paddr & PAGE_FRAME;
		new_paddr = PADDR_TO_KVADDR(new_paddr);
		old_vaddr = USERSTACK - ((i+1) * PAGE_SIZE);
		
		memmove((void *)(new_paddr),
			(const void *) (old_vaddr),
			PAGE_SIZE);
		/*
		memmove((void *)PADDR_TO_KVADDR(get_ppage(USERSTACK - ((i+1) * PAGE_SIZE), newas->pid)),
			(const void *) (USERSTACK - ((i+1) * PAGE_SIZE)),
			PAGE_SIZE);
			*/
	}
	//DEBUG(DB_VM, "Moving on.\n");
	
	// Go through the heap pages and copy everything over
	// Heap goes up from heapbase
	for(;newas->as_heaptop < old->as_heaptop;newas->as_heaptop+=PAGE_SIZE) 
	{
		memmove((void *) (PADDR_TO_KVADDR((get_ppage(newas->as_heaptop, pid)) & PAGE_FRAME)), (const void *)newas->as_heaptop,PAGE_SIZE);
    }
	//DEBUG(DB_VM, "Moving on.\n");
	
	// Copy the attribute bits?
	
	*ret = newas;
	return 0;
}

/*
*    as_destroy - dispose of an address space. You may need to change
*                the way this works if implementing user-level threads.
*/
void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	
	kfree(as);
}

/*
*    as_activate - make the specified address space the one currently
*                "seen" by the processor. Argument might be NULL, 
*		  meaning "no particular address space".
*/
void
as_activate(struct addrspace *as)
{
	//DEBUG(DB_VM, "AS activating...\n");
	// This is where we invalidate the TLB
	TLB_Invalidate_all();
	//DEBUG(DB_VM, "TLB Invalidated...\n");
	// Nothing else to do here?

	(void)as;  // suppress warning until code gets written
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;
	
	//DEBUG(DB_VM, "AS defining region...\n");
		
	/* Note that we can arbitrarily set the number of pages we allocate
		and later swap code/data in and out, rather than the whole thing
		at once. */

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;

		// Adjust the heap
		as->as_heaptop = as->as_heapbase = vaddr + sz; 
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		
		// Adjust the heap
		as->as_heaptop = as->as_heapbase = vaddr + sz; 
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

/*
*    as_prepare_load - this is called before actually loading from an
*                executable into the address space.
*/
int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	int i;
	u_int32_t new_paddr;
	u_int32_t virtual_addr;
		
	//assert(as->as_stackpbase == 0);
	//DEBUG(DB_VM, "AS preparing load...\n");
	//DEBUG(DB_VM, "Allocating %d code pages...\n", as->as_npages1);


	// Handle code segment first
	for (i=0; i < as->as_npages1; i++)
	{
		// Allocate each page
		//kprintf("Getting virtual address...\n");
		if(mips_vm_enabled==0)
		{
			new_paddr = getppages(1);
		}
		else
		{
			virtual_addr = as->as_vbase1 + (i*PAGE_SIZE);
			//kprintf("Getting a new physical address...\n");
			new_paddr = alloc_page(virtual_addr, as->pid);	
			
			//DEBUG(DB_VM, "Allocating virtual code address to core map == 0x%x\n", virtual_addr);
			//DEBUG(DB_VM, "Corresponds to physical code address == 0x%x\n\n", new_paddr);
			new_paddr = new_paddr & PAGE_FRAME;
			//DEBUG(DB_VM, "Corresponds to physical code with page frame address == 0x%x\n\n", new_paddr);
			
		}

		if(new_paddr == 0)
		{
			return ENOMEM;		
		}
		else
		{
			(void)new_paddr;
			//bzero?
		}
	}

	//DEBUG(DB_VM, "Allocating %d data pages...\n", as->as_npages2);
	// Now the data segment
	for (i=0; i < as->as_npages2; i++)
	{
		// Allocate each page
		if(mips_vm_enabled==0)
		{
			new_paddr = getppages(1);
		}
		else
		{
			virtual_addr = as->as_vbase2 + (i*PAGE_SIZE);
			new_paddr = alloc_page(virtual_addr, as->pid);
			//DEBUG(DB_VM, "Adding virtual code address to core map == 0x%x\n", virtual_addr);
			///DEBUG(DB_VM, "Corresponds to physical code address == 0x%x\n", new_paddr);
		
		}

		if(new_paddr == 0)
		{
			return ENOMEM;		
		}
		else
		{
			(void)new_paddr;
			//bzero?
		}

	}
		
	//DEBUG(DB_VM, "Allocating %d user stack pages...\n", VM_STACKPAGES);
	// And the stack (stack goes down)
	for (i=0; i < VM_STACKPAGES; i++) // May need to start at 1 instead of 0
	{
		// Allocate each page
		if(mips_vm_enabled==0)
		{
			new_paddr = getppages(1);
		}
		else
		{
			virtual_addr = USERSTACK - ((i+1)*PAGE_SIZE);
			new_paddr = alloc_page(virtual_addr, as->pid);
			// Put the new address into the core map
			//DEBUG(DB_VM, "Adding virtual code address to core map == 0x%x\n", virtual_addr);
			//DEBUG(DB_VM, "Corresponds to physical code address == 0x%x\n", new_paddr);
			//add_ppage(virtual_addr, new_paddr, as->pid, CLEAN);
		}

		if(new_paddr == 0)
		{
			return ENOMEM;		
		}
		else
		{
			(void)new_paddr;
			//bzero?
		}
	}
	
	//DEBUG(DB_VM, "AS preparing load done...\n");
	return 0;
}

/*
*    as_complete_load - this is called when loading from an executable
*                is complete.
*/
int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	//DEBUG(DB_VM, "AS completing load...\n");
	// Nothing needed here?

	(void)as;
	return 0;
}

/*
*    as_define_stack - set up the stack region in the address space.
*                (Normally called *after* as_complete_load().) Hands
*                back the initial stack pointer for the new process.
*/
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	//DEBUG(DB_VM, "AS defining stack...\n");
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	
	return 0;
}

#endif

