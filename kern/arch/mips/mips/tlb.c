#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <clock.h>

/*
 * Handles specific TLB Manipulation Functions
 * Added by tocurtis
 *
*/
#define RND 0
#define NRU 1 // Not recently used
#define TLB_REPLACEMENT_ALGO RND

void TLB_Init()
{
	if (TLB_REPLACEMENT_ALGO == RND)
		kprintf("TLB replacement algorithm: RANDOM\n");
	else
		kprintf("TLB replacement algorithm: NRU\n");
}


int TLB_Insert(vaddr_t faultaddress, paddr_t paddr)
{
	
	int i;
	u_int32_t ehi, elo;
	u_int32_t nru_entry;
    time_t sec; 
    u_int32_t nsec;
    
	
	// Look for an invalid entry on the TLB
	for (i=0; i<NUM_TLB; i++) {
		TLB_Read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "TLB Added: 0x%x -> 0x%x at location %d\n", faultaddress, paddr, i);
		TLB_Write(ehi, elo, i);
		//splx(spl); // Leave that to calling function
		return 0;
	}
	
	// No invalid entries
		
	switch(TLB_REPLACEMENT_ALGO)
        {
            //Least recent seen page replacement algorithm
            case NRU:
            {
                /*Get current time in sec and nanosec*/
				gettime(&sec, &nsec);        
				u_int32_t currsec=sec, currnsec= nsec;
				// Look for the fault on the TLB
				for (i=0; i<NUM_TLB; i++) {
					if(TLB_Probe(faultaddress, 0))
					{
						// This is the one
						// Mark it as recently used
						tlb_age[i] = nsec;
					}
				}
				nru_entry = 0;
				//Search for the oldest TLB entry
				for(i=0;i< NUM_TLB;i++)
				{
					if(  tlb_age[i] <= currnsec)
					{
							currnsec = tlb_age[i];
							//currnsec = coremap[i].last_access_time_nsec;
							nru_entry = i;
					}
				}
				
				// Now put it in a spot not recently used 	
				ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "TLB Added: 0x%x -> 0x%x\n", faultaddress, paddr);
				DEBUG(DB_VM, "\n\nReplacing entry %d on TLB.\n\n", nru_entry);
				TLB_Write(ehi, elo, nru_entry);
				
				
                break;
            }
            //By default rnd is used
            default:
			{
                ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "vm randomly added to slot.\n");
				TLB_Random(ehi, elo);
			}
        }                        	
	
	return 0;

}

int TLB_Invalidate_all()
{
	// Code to invalidate here
	int i;
	int spl;
	
	//u_int32_t ehi, elo;
	//DEBUG(DB_VM, "Invalidating TLB.\n");
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	if (TLB_REPLACEMENT_ALGO == NRU)
		for (i=0; i<NUM_TLB; i++)
			tlb_age[i] = 0;
	splx(spl);

	/*
	// Go through the table and invalidate each entry
	for (i=0; i<NUM_TLB; i++) {
		TLB_Read(&ehi, &elo, i);
		
		// If we need to update the pages represented by the TLB
		//  we will do it here
		// I'm not sure how that will work at this point
		// For now I'll skip it but it will cause some errors I imagine
		DEBUG(DB_VM, "tlb will invalidate: 0x%x -> 0x%x\n", ehi, elo);
		ehi = TLBHI_INVALID(i);
		elo = TLBLO_INVALID();
		DEBUG(DB_VM, "tlb invalidated: 0x%x -> 0x%x\n", ehi, elo);
		TLB_Write(ehi, elo, i);
		//splx(spl); // Leave that to calling function
		
	}
	*/
	return 0;
}

int TLB_Invalidate(paddr_t paddr)
{
    u_int32_t ehi,elo,i;
    for (i=0; i<NUM_TLB; i++) 
    {
        TLB_Read(&ehi, &elo, i);
        if ((elo & 0xfffff000) == (paddr & 0xfffff000))	
        {
            TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);		
        }
    }

    return 0;
}
