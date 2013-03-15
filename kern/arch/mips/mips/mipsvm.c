#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>

/*
 * Note: This code is copied from dumbvm.c and will be modified for our use
 *
 *
*/

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */


#if OPT_DUMBVM
//do nothing
#else

static int last_claimed_index;

//static
paddr_t
getppages(unsigned long npages)
{
	int spl;
	paddr_t addr;

	spl = splhigh();

	addr = ram_stealmem(npages);
	
	splx(spl);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	vaddr_t vaddr;
	
	if(mips_vm_enabled)
	{
		//Use our vm
		//		pa = getnpages(npages);
		pa=kpage_nalloc(npages,curthread->pid);
	}
	else
	{
		pa = getppages(npages);
		//DEBUG(DB_VM, "Allocated kernal space for %u pages in dumbvm.c.\n", npages);
	}
		
	
	//pa = getppages(npages);
	//	DEBUG(DB_VM, "Allocated kernal space for %u pages in dumbvm.c.\n", npages);
	
	if (pa==0) 
	{
		return 0;
	}
	else
	{
		vaddr = PADDR_TO_KVADDR(pa);
	}

	/*update the claimed frames holder*/
	if(npages>1)
	{		
		claimed_frames[last_claimed_index].vaddr=vaddr;
		claimed_frames[last_claimed_index].npages=npages;
		last_claimed_index++;	        
	}
	
	return vaddr;
}

void 
free_kpages(vaddr_t addr)
{
    	int i;
	int index = 0;
		
	/*search for the frame*/
	for(i=0;i<last_claimed_index;i++)
	{
		if(claimed_frames[i].vaddr==addr)
		{
			index=i;
			break;
		}
	}

	if(index!=0)
	{	
		/*remove the physical pages*/
		for(i=0;i < (int)claimed_frames[index].npages; i++){	
			remove_ppage( (addr+(i*PAGE_SIZE))-MIPS_KSEG0);		
		}	

		/*clear the claimed frame slot in the holder*/
		claimed_frames[index].npages=0;
		claimed_frames[index].vaddr=0;

		/*update the holder to make the frames started from last_claimed_index as free*/
		for(i=index;i < (int)last_claimed_index;i++) {
			claimed_frames[index]=claimed_frames[index+1];
		}
	}
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	// Do not use any user space references!
	// No copyin/out, no kprintf, etc.
	
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr=0;
	int i;
	u_int32_t ehi, elo;
	struct addrspace *as;
	int spl;
	//int random_choice;

	//spl = splhigh(); 

	// Lock it here? If so, unlock before each return

	faultaddress &= PAGE_FRAME;

        if(faultaddress == 0)
        {
                //DEBUG(DB_VM, "vm_fault: 0x%x\n", faultaddress);
                return 0;
        }

	//DEBUG(DB_VM, "vmfault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("vm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		//splx(spl);
		return EINVAL;
	}

	// READ and WRITE faults will fall through to here
	//DEBUG(DB_VM, "Getting current thread...\n");
	as = curthread->t_vmspace;
	if (as == NULL) {
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		DEBUG(DB_VM, "No address space set up!\n");
		return EFAULT;
	}
	//DEBUG(DB_VM, "Checking assertions...\n");
	/* Assert that the address space has been set up properly. */
	assert(as->as_vbase1 != 0);
	//assert(as->as_pbase1 != 0);
	assert(as->as_npages1 != 0);
	assert(as->as_vbase2 != 0);
	//assert(as->as_pbase2 != 0);
	assert(as->as_npages2 != 0);
	//assert(as->as_stackpbase != 0);
	assert((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	//assert((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	assert((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	//assert((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	//assert((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	// Note that we might need more assertion checks for our other attributes here
	
	//check if we are not touching kernel segment
	if(faultaddress <= MIPS_KSEG0)
	{
		
		paddr = handle_page_fault(faultaddress);
		// Verify we found a paddr
		if (paddr == 0)
			panic("Can't find the faultaddress on the core map!\n");

		/* make sure it's page-aligned */
		//DEBUG(DB_VM, "Before assert: paddr == 0x%x\n", paddr);

		paddr = paddr & PAGE_FRAME;
		//DEBUG(DB_VM, "with page_frame == 0x%x\n", (paddr & PAGE_FRAME));
		//assert((paddr & PAGE_FRAME)==paddr);

		//DEBUG(DB_VM, "Inserting into TLB...\n");
		/* Disable interrupts before modifying tlb table. */
		spl = splhigh(); 
		// Insert the page into the TLB
		TLB_Insert(faultaddress, paddr);
		splx(spl);		
		//DEBUG(DB_VM, "Inserted into TLB.\n");
		return 0;
	}
	else
	{
		kprintf("VM_FAULT: Kernel Page 0x%x\n",faultaddress);
		return 0;
	}

	//splx(spl); 
	return EFAULT;
}

#endif
