#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>
#include <bitmap.h>

#include "kern/types.h"

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

/*********************************************************************
 * Virtual Memory Implementation: Paging, Swapping, amd TLB handling *
 * modified by rahmanmd                                                 *
 *********************************************************************/

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

paddr_t getppages(unsigned long npages);
/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);


//added by rahmanmd
/*TLB Structure, top bit of VPN is always zero to indicate User segment*/
//<----------------20------------------->|<----6---->|<----6---->|
//_______________________________________________________________
//|0|     Virtual Page Number            |   ASID    |     0     |  EntryHi
//|______________________________________|___________|___________|
//|       Page Frame Number              |N|D|V|G|       0       |  EntryLo
//|______________________________________|_______|_______________|



/*20 bit Page address*/
//<----------------20------------------->|<---------12---------->|
//_______________________________________________________________
//|           Page Address               |N|D|V|G|0|0|0|0|0|0|0|K|  
//|______________________________________|_______|_______|_______|
/*Macros for managing attibute bits of a page entry*/
#define IS_KERNEL(x) ((x) & 0x00000001)
#define SET_KERNEL(x) ((x) | 0x00000001)

#define IS_VALID(x) ((x) & 0x00000200)
#define SET_VALID(x) ((x) | 0x00000200)

#define IS_DIRTY(x) ((x) & 0x00000400)
#define SET_DIRTY(x) ((x) | 0x00000400)

#define ISSWAPPED(x) ((x) & 0x00000080)
#define SET_SWAPPED(x) ((x) | 0x00000080)
/*
 * In order to manage the physical page frames, We will maintain a core map, 
 * a sort of reverse page table. Instead of being indexed by virtual addresses, 
 * a core map is indexed by its physical page number and contains the virtual 
 * address and address space identifier for the virtual page currently backed 
 * by the page in physical memory. So, we can use a array of _PTE structure 
 * defined below as the coremap. 
 */
struct _PTE {
	paddr_t paddr; //physical address of the page
	vaddr_t vaddr; //virtual address of the page
	pid_t pid; //process id of the process sharing the page. We don't need to store address space pointer as we can index into process table using pid and can get the address space by accessing the thread structure.
	//u_int64_t last_accces_time; //last accessed time
        u_int32_t last_access_time_sec; //last accessed time (sec)
        u_int32_t last_access_time_nsec; //last accessed time (fraction of second)
	u_int32_t status; //page status: Kernel, free, dirty, clean, etc.
};
/*Initialize the physical memory coremap*/
void init_coremap();
/*
 * Add an inverse entry for the physical page associated with mapping from vaddr to 
 * paddr into the coremap. Inverse mapping means the page is indexed by page
 * number calculated using paddr.
 */
int add_ppage (u_int32_t vaddr, u_int32_t paddr, pid_t pid, u_int32_t status);
/*remove the page table entry mapped with paddr, */
int remove_ppage (u_int32_t paddr);


/*We are using  disk0 to store the swapped pages*/
#define SWAP_FILE "lhd0raw:";
/*Initialize the swap area map*/
void init_swaparea();
/*
 * Add an inverse entry for the swapped out page associated with mapping from 
 * vaddr to paddr into the chunk of the swaparea.
 */
int add_spage (u_int32_t vaddr, u_int32_t chunk, pid_t pid);
/*remove the swapped in page from the swap area chunk*/
int remove_spage (u_int32_t chunk);

/*
 * Swapin()
 * -----------------------
 * 1. Sanity checks: We can't swap the pages holding the page table itself. 
 *    So, check if the paddr lie outside of coremap or not.
 * 2. We use mk_kuio to intiate a read from disk to physical memory.
 * 3. Remove the mapping of the page in the swaparea and unmark the swapmap 
 *    bitmap.
 * 4. Read into the page from disk.
 */
void swapin(u_int32_t paddr, u_int32_t chunk);

/*
 * Swapout()
 * -----------------------
 * 1. Sanity checks: We can't swap the pages holding the page table itself. 
 *    So, check if the paddr lie outside of coremap or not.
 * 2. We use mk_kuio to intiate a write to disk from the physical memory.
 * 3. insert the mapping of the page in the swaparea and mark the swapmap bitmap.
 * 4. Write out the page into disk.
 * 5. Invalidate all the tlb entries by writing TLBHI_INVALID(i) and 
 *    TLBLO_INVALID() into tlb entries.
 */
void swapout(u_int32_t chunk, u_int32_t paddr);

/*
 * Search the swaparea for the disk resident page addressed by vaddr and 
 * return the paddr of that page. If page doesn't exist then we are in trouble.
 * So, panic if the page is not found in disk as page is supposed to be present 
 * either in disk or in memory. 
 */
u_int32_t get_spage(u_int32_t vaddr, pid_t pid);

/*
 * Bring back the page from disk into memory by swapping out a victim page if
 * necessary.
 */
u_int32_t load_page_into_memory(u_int32_t vaddr, pid_t pid);

/* 
 * This is a core function of our vm. It is responsible to bring the demanded 
 * page into memory and return the physical address of the page addressed by 
 * vaddr by looking into the page table. If the page is not in memory then it'll 
 * get the page from disk into memory. If memory is full the will find a victim 
 * page to be swapped out.
 * 
 * 1. Check coremap to see whether the page actually present (searching), if not 
 * then legal page fault. Do handle the fault as follows:
 * 	2.1. As we are loading segment into memory during loadelf(), and during 
 *           page fault the page is not in memory, so the page must be in disk. 
 * 	2.2. Find the chunk of the demanded page in swaparea.
 * 	2.3. Find a free page from the coremap. If no free page then swap out a 
 *           victim page using algorithms described earlier (see alloc_page()). 
 *           Swapped out page should be written to disk propeerly (if dirty) 
 *           and its address is returned. Make sure to insert the swapped out \
 *           page address into the swaparea and unmark the page in swapmap 
 *           properly.
 * 	2.3. Swap-in the demanded page from disk to memory at the free pageaddr 
 *           returned. 
 * 	2.4. Make sure to update the coremap to insert the swapped-in page and 
 *           properly mark the page in the bitmap for coremap pages.
 * 	2.5. return the paddr of the page.
 * 3. Check whether the returned paddr is a valid address. If valid return paddr, Otherwise panic.
 */
u_int32_t get_ppage(u_int32_t vaddr, pid_t pid);

/* 
 * This is the interface of our vm to handle tlb/page fault() by calling the
 * get_ppage() to bring the page into memory. It is responsible for updating 
 * the last access time of the page to make our LRU page replacement working.
 */
u_int32_t handle_page_fault(u_int32_t vaddr);

/*
 * alloc_page(): allocate a single page:
 * -------------------------------------
 * 1. Scan the coremap for a free page. 
 * 2. If we find a free page then we just mark the page as dirty (as the page 
 *    is not yet written on disk) and return the paddr of the free page.
 * 3. If we can't find a free page then we should swap out a page from coremap. 
 * 4. We can use different algorithm to choose the victim page to be swapped 
 *    out of the memory.
 * 
 * 	1. LRU page replacement Algorithm:
 * 	==================================
 * 	We can keep track of the timestamp of accessing a page and then use 
 *      this information to evict the least recent used page to be swapped out of the physical memory to disk.
 * 
 * 	2. Random page replacement Algorithm:
 * 	=====================================
 * 	Replaces a random page in memory.
 * 
 * 5. Once we have found a victim page to swapped out (discussed later) the 
 *    page into a free swap chunk and mark the swapmap appropriately. Return 
 *    the paddr of this page.
 * 6. Insert the page into the coremap and mark the bitmap properly.
 */

u_int32_t alloc_page(u_int32_t vaddr, int pid);
        
/* Allocate n contiguous kernel pages */
u_int32_t kpage_nalloc(int n, int pid);

/*Max number of active processes, should be the size of the tlb cache*/
#define MAX_ACTIVE_PROCESSES 64
/*contains the allocation status of contagious frames held by a process*/
struct contagious_frames{
vaddr_t vaddr;
size_t npages;
};

/*Page fault statistics*/

int total_tlb_faults;
int total_page_faults;
int total_asyncpage_write;

/*contagious allocation information of the active processes*/
struct contagious_frames *claimed_frames;

/*
 * it should be enabled after the coremape and swaparea have been allocated 
 * because we can't use kmalloc properly untill we init our paging mechanism 
 * and make alloc_kpages() work. So, initializing the coremap and the swaparea 
 * will be done by calling kmalloc() with dumbvm and so it will use getppages() 
 * i.e. ram_stealmem(npages) to allocate space for swaparea and coremap.
 */
int mips_vm_enabled;
        
#endif /* _VM_H_ */
