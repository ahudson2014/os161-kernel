#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <bitmap.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>
#include <kern/stat.h>
#include <uio.h>
#include <machine/spl.h>
#include <generic/random.h>
#include <clock.h>
#include <machine/trapframe.h>
#include <kern/unistd.h>
#include <machine/vm.h>
#include <machine/tlb.h>
#include <machine/bus.h>
#include <thread.h>
#include <curthread.h>

#if OPT_DUMBVM
//do nothing
#else

/*********************************************************************
 * Virtual Memory Implementation: Paging, Swapping, amd TLB handling *
 * added by rahmanmd                                                 *
 *********************************************************************/

/*
 * In order to manage the physical page frames, We will maintain a core map, 
 * a sort of reverse page table. Instead of being indexed by virtual addresses, 
 * a core map is indexed by its physical page number and contains the virtual 
 * address and address space identifier for the virtual page currently backed 
 * by the page in physical memory. So, we can use a array of _PTE structure 
 * defined below as the coremap. 
 */
struct _PTE *coremap; //array of page table entries for storing physical pages.
/*
 * We use a bitmap of size #pages to manage the used/free pages in the 
 * coremap. Once a page is allocated/removed we mark/unmark the bitmap indexed 
 * by the hashed pagenumber. In this way we can effciently maintain the 
 * used/free frames (using bitmap_alloc() to find a avilable slot etc.). 
 */
struct bitmap *core_memmap;
int coremap_size;/*size of coremap*/
/*
 * Address of the physical memory where the coremap itself is stored. So the
 * coremap is starting from this address.
 */
u_int32_t coremap_base;

/*
 * We will need to store evicted pages and find them when they needed to be 
 * swapped in. We use an array of page table entry to store swapped out 
 * pages.
 */
struct _PTE *swaparea;
struct bitmap *swap_memmap;/*we maintain a bitmap to describe the swap area.*/
struct vnode *swap_fp;/*file pointer to the disk location of the swaparea*/
int swaparea_size;/*Size of swap area*/
u_int32_t swap_base;//starting address of the swap area
//int mips_vm_enabled = 0;
/*
 * Page replacement algorithms
 */
#define RND 0
#define LRU 1
#define PAGE_REPLACEMENT_ALGO RND

//Page status
typedef enum
{    
    PAGE_FREE,
    PAGE_DIRTY,
    PAGE_CLEAN,
    PAGE_KERNEL
} PAGE_STATUS;

/*
 * Initialize the coremap and swaparea
 */
void init()
{        
    mips_vm_enabled = 0;
    claimed_frames = (struct contagious_frames*)kmalloc(MAX_ACTIVE_PROCESSES*sizeof(struct contagious_frames));	
    init_swaparea();	    
    init_coremap();
	TLB_Init();
    //now enable our vm
    mips_vm_enabled = 1;
}

/*
 * Initialize the virtual memory module
 */
void vm_bootstrap(void)
{    
    //open the disk as a file and use the whle disk0 as the swap space.
    char file_name[] = SWAP_FILE;     
    int result = vfs_open(file_name, O_RDWR , &swap_fp);
    if(result)
        panic("VM: Failed to create Swap area\n");
    
    //initialize the paging mechanism
    init();
    if(PAGE_REPLACEMENT_ALGO == LRU)
	    kprintf("Page replacement algorithm: LRU\n\n");
    else
	    kprintf("Page replacement algorithm: RANDOM\n\n");
}

/*
 * Initialize the swap area and the bitmap to describe the area. We steal memory 
 * from ram to store these data structures. Initialize each of the page's chunk
 * address.
 */
void init_swaparea() 
{
    int i;
    struct stat file_stat;    
    //read the size of the disk0 into file_stat
    VOP_STAT(swap_fp, &file_stat);   
    
    //swaparea size is in PAGE_SIZE unit
    swaparea_size = file_stat.st_size/PAGE_SIZE;   
    //bitmap to describe the swaparea (in memory or in disk)
    swap_memmap = bitmap_create(swaparea_size);    
    //allocate the swaparea into memory by stealing some memory from RAM
    swaparea = (struct _PTE*)kmalloc(swaparea_size * sizeof(struct _PTE));    

    //we are initializing swaparea first, so base address should be 0
    swap_base = 0;    
    //set chunk of each of the pages in the swaparea
    for(i = 0; i < file_stat.st_size/PAGE_SIZE; i++) 
    {
        swaparea[i].paddr = (swap_base + (i * PAGE_SIZE));
    }    
}

/*
 * Initialize the coremap and bitmap to describe the page table. We steal memory
 * from RAM to store these structures. Initialize each of the entry of the page
 * table with the page address with respect to the base address of the coremap
 * stored in RAM.
 */
void init_coremap() 
{
    int i;
    u_int32_t ram_max = mips_ramsize();
    u_int32_t ram_user_base = ram_stealmem(0);
    
    //size of the coremap in PAGE_SIZE unit
    coremap_size = (ram_max-ram_user_base-PAGE_SIZE)/PAGE_SIZE;
    
    //bitmap to keep track of the availability state of the coremap memory
    core_memmap = bitmap_create(coremap_size);
    //allocate the coremap into memory by stealing some memory from RAM
    coremap = (struct _PTE*)kmalloc(coremap_size * sizeof(struct _PTE));
    
    //base address for the coremap in the ram, it starts from where the swaparea
    //ended
    coremap_base = ram_stealmem(0);
    //set each of the page address
    for(i = 0; i < coremap_size; i++) 
    {
        coremap[i].paddr = (coremap_base + (i * PAGE_SIZE));
        coremap[i].status = PAGE_FREE;
        coremap[i].pid = 0;
    }   
}

/*
 * Add an inverse entry for the physical page associated with mapping from vaddr 
 * to paddr into the coremap. Inverse mapping means the page is indexed by page
 * number calculated using paddr.
 */
int add_ppage (u_int32_t vaddr, u_int32_t paddr, pid_t pid, u_int32_t status)
{
    int result = 0;
    
    int spl=splhigh();    
    paddr = paddr & PAGE_FRAME;
    
    //get the index of the page in the page table
    int page_index = (paddr - coremap_base) / PAGE_SIZE;
    //make sure that the paddr address is valid
    assert( (coremap[ page_index ].paddr & PAGE_FRAME) == paddr );
    
    /*
     * Add the mapping and set the attribute bits
     */
    coremap[ page_index ].vaddr = vaddr;
    //If it is a kernel address allocated by kernel then set kernel attribute flag
    if(vaddr > USERTOP)
        coremap[ page_index ].paddr = SET_VALID(paddr)|SET_DIRTY(paddr)|SET_KERNEL(paddr);
    else
        coremap[ page_index ].paddr = SET_VALID(paddr)|SET_DIRTY(paddr);
    
    /*Initialize _PTE fields for this entry*/
    coremap[ page_index ].last_access_time_sec = 0;
    coremap[ page_index ].last_access_time_nsec = 0;
    coremap[ page_index ].pid = pid;
    coremap[ page_index ].status = PAGE_DIRTY;
    
    /*
     * mark (unavailable) the page entry of coremap.
     */
    if(!bitmap_isset(core_memmap,page_index))
        bitmap_mark(core_memmap, page_index);    
    splx(spl);
        
    return result;
}

/*remove the page table entry mapped with paddr, */
int remove_ppage (u_int32_t paddr)
{
    int result = 0;    
    
    int spl=splhigh();
    paddr = paddr & PAGE_FRAME;
    
    //get the index of the page in the page table
    int page_index = (paddr - coremap_base) / PAGE_SIZE;
    //make sure that the paddr address is valid
    assert((coremap[ page_index ].paddr & PAGE_FRAME) == (paddr & PAGE_FRAME));
    
    /*
     * Clear the _PTE fields for this entry
     */
    coremap[ page_index ].vaddr = 0;
    coremap[ page_index ].paddr = paddr & PAGE_FRAME;
    coremap[ page_index ].last_access_time_sec = 0;
    coremap[ page_index ].last_access_time_nsec = 0;
    coremap[ page_index ].pid = 0;
    coremap[ page_index ].status = PAGE_FREE;
    
    /*
     * umnark the bit of the core memory map to indicate that the page is free
     */
    bitmap_unmark(core_memmap, page_index);
    splx(spl);	
    
    return result;
}

/*
 * Add an inverse entry for the swapped out page associated with mapping from 
 * vaddr to chunk into the swaparea.
 */
int add_spage (u_int32_t vaddr, u_int32_t chunk, pid_t pid)
{
    int result = 0;
        
    //get the index of the chunk in the swap area
    int chunk_index = (chunk & PAGE_FRAME) / PAGE_SIZE;
    //make sure that the chunk address is valid
    assert( (swaparea[ chunk_index ].paddr & PAGE_FRAME) == chunk );
    if (pid == 0)
		panic("PID = 0 in add_spage!");
    /*
     * Insert a mapping (invert index) of the page addresses by vaddr into the
     * swap area mapping indexed by the chunk
     */
    int spl=splhigh();
    swaparea[ chunk_index ].vaddr = vaddr;
    swaparea[ chunk_index ].last_access_time_sec = 0;
    swaparea[ chunk_index ].last_access_time_nsec = 0;
    //swaparea[ chunk_index ].pid = curthread->pid;
    swaparea[ chunk_index ].pid = pid; // changed by tocurtis

    /*
     * mark (as non-empty) the bitmap describing the swap area chunk
     */
    if(!bitmap_isset(swap_memmap, chunk_index))
        bitmap_mark(swap_memmap, chunk_index);
    
    splx(spl);    

    return result;
}

/*remove the swapped in page from the swap area mapping*/
int remove_spage (u_int32_t chunk)
{
    int result = 0;        
    
    //get the index of the chunk in the swap area
    int chunk_index = (chunk & PAGE_FRAME) / PAGE_SIZE;
    //make sure that the chunk address is valid
    assert( (swaparea[ chunk_index ].paddr & PAGE_FRAME) == chunk );
    
    /*
     * Clear the swapmap for this chunk
     */
    int spl=splhigh();
    swaparea[ chunk_index ].vaddr = 0;
    swaparea[ chunk_index ].paddr = chunk;
    swaparea[ chunk_index ].last_access_time_sec = 0;
    swaparea[ chunk_index ].last_access_time_nsec = 0;
    swaparea[ chunk_index ].pid = 0;	
    
    /*
     * unmark the memmap for the chunk to indicate that the chunk is free.
     */
    bitmap_unmark(swap_memmap, chunk_index);
    splx(spl);
    
    return result;
}

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
void swapin(u_int32_t paddr, u_int32_t chunk)
{
    /*
     * sanity check: make sure that we are not touching kernel space or the page
     * table itself .That is the page should be within the coremap memory 
     * starting from coremap_base
     */    
    assert(paddr >= coremap_base);
    
    int spl=splhigh();
    /*
     * Initialize the read I/O into kernel buffer of size PAGE_SIZE starting 
     * from paddr from the swaparea starting from offset indexed by chunk.
     */    
    struct uio swap_uio;
    mk_kuio(&swap_uio, /*kernel buffer*/(void*)PADDR_TO_KVADDR(paddr & PAGE_FRAME), 
                       /*Size of the buffer to read into*/PAGE_SIZE, 
                       /*Starting offset of the swap area for read out */chunk, UIO_READ);        
    
    /*
     * Remove the mapping of the chunk to page in the swaparea and unmark the 
     * swap_memmap bitmap to free the chunk.
     */
    remove_spage(chunk);
    splx(spl);
    
    //Now we read the page from memory into kernel buffer pointed with paddr
    int result=VOP_READ(swap_fp, &swap_uio);
    if(result) 
        panic("VM: SWAPIN Failed");    
}

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
void swapout(u_int32_t chunk, u_int32_t paddr)
{
    /*
     * sanity check: make sure that we are not swapping out any kernel page or
     * the address space containing the page table itself.That is the page 
     * should be within the coremap memory starting from coremap_base
     */    
    assert(paddr >= coremap_base);
    
    int spl=splhigh();    
    /*
     * Initialize the write I/O from kernel buffer of size PAGE_SIZE starting 
     * from paddr into the swaparea starting from offset indexed by chunk.
     */
    struct uio swap_uio;
    mk_kuio(&swap_uio, /*kernel buffer*/(void*)PADDR_TO_KVADDR(paddr & PAGE_FRAME), 
                       /*Size of the buffer to writeout*/PAGE_SIZE, 
                       /*Starting offset of the swap area for write into */chunk, UIO_WRITE);
    
    /*
     * Before actual write we should mark the swap area as not-empty to avoid 
     * inconsistency and add the swapped page into the mapping from the page to 
     * this chunk.
     * 
     */     
    //get the physical page
    struct _PTE ppage = coremap[(paddr-coremap_base)/PAGE_SIZE];
	if (ppage.pid == 0)
	{
		panic("PID in swapout == 0!");
		//DEBUG(DB_VM, "\npid = 0 at coremap[%u] (paddr=0x%x).\n\n", (paddr-coremap_base)/PAGE_SIZE, paddr);
		//ppage.pid = curthread->pid; // Workaround by tocurtis
	}
    DEBUG(DB_VM, "Putting page at address 0x%x into swap area with pid %d.\n", ppage.vaddr, ppage.pid);
	
	//add and mark into swaparea
    add_spage(ppage.vaddr, chunk, ppage.pid);
    splx(spl);    
    
    /*
     * Now, do the actual writing out the page into disk, we can avoid this 
     * write if the page is present on disk and it is not dirty. For now we are 
     * not checking this and always writing to disk (i.e. if already in disk 
     * then we are overwriting it).      
     */    
    int result=VOP_WRITE(swap_fp, &swap_uio);
    if(result)     
        panic("VM_SWAP_OUT: Failed");   
    
    //Invalidate the corresponding TLB entry in the associative cache
    spl=splhigh();
    TLB_Invalidate(paddr);
    splx(spl);
}

/*Random Page replacement algorithm*/
u_int32_t replace_rnd_page () 
{
    int spl=splhigh();
    u_int32_t a_page;
    /*
     * Get a random page to replace.
     * Make sure that we are not replacing kernel space pages, kernel pages
     * are fixed, non-mapped.
     */
    do
    {
        a_page = random()%coremap_size;		
    }while(IS_KERNEL(coremap[a_page].paddr));
    
    splx(spl);
    
    /*Sanity check: Kernel page can't be swapped out*/
    if(coremap[a_page].vaddr > USERTOP)
        panic("VM_LRU_PAGE_REPLACE: SWAPPING OUT KERNEL PAGE");
    
    return(coremap[a_page].paddr);
}

/*
 * Least Recent Used page replacement algorithm.
 */
u_int32_t replace_lru_page () 
{
    int spl=splhigh();

    int i;
    u_int32_t lru_page;
    time_t sec; 
    u_int32_t nsec;
    /*Get current time in sec and nanosec*/
    gettime(&sec, &nsec);        
    u_int32_t currsec=sec, currnsec=nsec;
    //u_int32_t currsec = system_counter;

    /*Search for the oldest 'userspace' page used*/
    for(i=0;i< (int)coremap_size;i++)
    {
        if(  !(IS_KERNEL(coremap[i].paddr))
             && (coremap[i].last_access_time_sec <= currsec)
             /*&& (coremap[i].last_access_time_nsec <= currnsec)*/)
        {
                currsec = coremap[i].last_access_time_sec;
                //currnsec = coremap[i].last_access_time_nsec;
                lru_page = i;
        }
    }

    splx(spl);
    
    /*Sanity check: Kernel page can't be swapped out*/
    if(coremap[lru_page].vaddr > USERTOP)
        panic("VM_LRU_PAGE_REPLACE: SWAPPING OUT KERNEL PAGE");

    return(coremap[lru_page].paddr);
}

/*
 * Get an empty chunk from the swap area to store a swapped out page 
 * We use a bitmap to describe the swap area. So, just look get an unmarked 
 * bit index from the swap_memmap and return the address of the chunk indexed 
 * by the index found. If there is no empty chunk then we are in great trouble.
 * We can't handle this request, so kill the thread and exit.
 */
u_int32_t get_empty_chunk() 
{
    int spl=splhigh();
    
    unsigned chunk_index;
    int no_free_chunk = bitmap_alloc(swap_memmap, &chunk_index);
    
    if(!no_free_chunk)
    {
        splx(spl);
        return (chunk_index*PAGE_SIZE);
    }    
    else
    {
        splx(spl);
        kprintf("VM: Swap Space full, killing curthread");
        sys__exit(0);
    }    
}

/*
 * This method is a vital method for our VM which is responsible to get a 
 * free page from the page table. If no free page is found then snatch a page.
 * The victim page can be selected according to different algorithm. We have
 * implemented two such plage replacement algorithm: Random and LRU. The 
 * snatched out page should be swapped out into disk. Return the page address.
 */
u_int32_t snatch_a_page() 
{
    int spl=splhigh();
    unsigned free_page_index;
    //get a free page index by looking into the memory map of the coremap
    int free_page_not_found = bitmap_alloc(core_memmap, &free_page_index);
        
    //A free entry is found
    if(!free_page_not_found)
    {
        paddr_t paddr = coremap[free_page_index].paddr;
        //if( IS_VALID(paddr)) 
        //{
            splx(spl);
            return paddr;
        //}        
        //panic("VM: Invalid Memory");
    }        
    else
    {
        //There is no free page available, so replace a victim page    
        u_int32_t paddr;
        
        switch(PAGE_REPLACEMENT_ALGO)
        {
            //Least recent seen page replacement algorithm
            case LRU:
            {
                paddr= replace_lru_page();
                break;
            }
            //Random page replacement algorithm
            case RND:
            {
                paddr= replace_rnd_page();
                break;
            }
            //By default rnd is used
            default:
                paddr= replace_rnd_page();
        }                        
        
	assert(paddr!=0x0);
        //Now, we have to actually swap out the old page to make the slot free
        //for the calling thread. There might be a possibility of race 
        //condition here (Ignore for mow, we'll come back to it later)
        if(IS_VALID(paddr)) 
        {
            splx(spl);
            
            //kprintf("swapout 0x%x\n", paddr);
            //get an empty chunk to swapout the replaced page
            u_int32_t chunk = get_empty_chunk();
            //now, swapout the replaced page, if not dirty then skip writing
            //to the disk. swapout will handle this
            swapout(chunk, paddr);            
            //TODO: Update no of asynchronous page write statistics (increment)
     	    total_asyncpage_write++;
            return paddr;
        }
        panic("VM: LRU ERROR");
    }            
}


/*
 * Search the swaparea for the disk resident page addressed by vaddr and 
 * return the chunk containing the page. If page doesn't exist then we are in 
 * trouble. So, panic if the page is not found in disk as page is supposed to 
 * be present either in disk or in memory. 
 */
u_int32_t get_spage(u_int32_t vaddr, pid_t pid)
{
    int i;    
    
    /*
     * search for the page in the disk (swaparea), if exists then return chunk 
     * offset otherwise we are in trouble. The page was supposed to be there.
     * So panic if the page is not in disk either.
     * 
     */
        int spl=splhigh();
        DEBUG(DB_VM, "Looking for page in swaparea...\n");
	DEBUG(DB_VM, "pid = %d.\n", pid);
	DEBUG(DB_VM, "curthread->pid = %d.\n", curthread->pid);
	
	for(i=0; i < (int)swaparea_size; i++) 
        {
        
		//sigh! I found the page on the disk. 
        //if(swaparea[i].vaddr == vaddr && (swaparea[i].pid == curthread->pid || swaparea[i].pid == curthread->t_vmspace->pid))
		if(swaparea[i].vaddr == vaddr && swaparea[i].pid == pid)
        {            
			DEBUG(DB_VM, "matched swap #%d with vaddr 0x%x and pid %d for search vaddr 0x%x and pid %d\n", i, swaparea[i].vaddr, swaparea[i].pid, vaddr, pid);
            splx(spl);
            //TODO: Update asynchronous write statistics 
	    //total_asyncpage_write++;           
            return(swaparea[i].paddr);
        }
        //But wait! address space matches but pid doesn't match! panic.
        if(swaparea[i].vaddr == vaddr) 
        {
            DEBUG(DB_VM, "Found matching vaddr 0x%x but doesn't match pid %d.\n", swaparea[i].vaddr, swaparea[i].pid);
			//splx(spl);
            //panic("VM_SPAGE: address space matches but pid doesn't match. 0x%x\n 0x%x %d %d \n",vaddr,swaparea[i].paddr,swaparea[i].pid,curthread->pid);
        }
    }
    splx(spl);
    //oh damn! page doesn;t exists in disk either. Panic. We may investigate 
    //the memory before panic.
    panic("VM: Invalid Address, I'll Die now 0x%x!!\n\n",vaddr);
    
    return 0;
}

/*
 * Bring back the page from disk into memory by swapping out a victim page if
 * necessary
 */
u_int32_t load_page_into_memory(u_int32_t vaddr, pid_t pid) 
{
    //panic("VM: hm......right...\n);
    //Get the chunk containing demanded page (not in memory) from disk
    u_int32_t chunk = get_spage(vaddr, pid);
    
    /*
     * snatch a entry in page table for this page by swapping out a victim page 
     * if necessary
     */    
    u_int32_t paddr = snatch_a_page();
    assert(paddr!=0x0);
    
    /*
     * Now, we have a free entry in the page table for this page. So bring 
     * back the page from disk by swapping the chunk into paddr.
     */    
    swapin(paddr, chunk);
    
    /*
     * set the attributes
     */
    paddr = SET_VALID(paddr);    
    paddr = SET_SWAPPED(paddr);
    
    /*
     * So, we have swapped in the page into memory. Add the pagetable entry for
     * this page.
     */    
    int spl=splhigh();
    assert((chunk & PAGE_FRAME)/PAGE_SIZE < swaparea_size);
    //add_ppage(vaddr, paddr, swaparea[(chunk & PAGE_FRAME)/PAGE_SIZE].pid, PAGE_CLEAN);
	add_ppage(vaddr, paddr, pid, PAGE_CLEAN); //Changed by tocurtis
    splx(spl);
    
    return paddr;	
}

/* 
 * This is a core function of our vm. It is responsible to bring the demanded 
 * page into memory and return the physical address of the page addressed by 
 * vaddr by looking into the page table. If the page is not in memory then it'll 
 * get the page from disk into memory. If memory is full the will find a victim 
 * page to be swapped out.
 * 
 */
u_int32_t get_ppage(u_int32_t vaddr, pid_t pid)
{
    u_int32_t paddr;    
    int i;
    
    int spl=splhigh();
    /*
     * search for the page in the page table, if exists then return paddr 
     * otherwise we need to bring the pageback into memory from disk by swapping
     * out a victim page to make place for the demanded page in memory
     * 
     */   
    for(i=0; i < (int)coremap_size; i++) 
    {
        //a match found
        if(coremap[i].vaddr == vaddr && (coremap[i].pid == pid || coremap[i].pid == 0)) 
        {          
            //DEBUG(DB_VM, "Found the page!\n\n");
			//a valid page fault
            if(IS_VALID(coremap[i].paddr)) 
            {             
		    //DEBUG(DB_VM, "matched coremap #%d with vaddr 0x%x and pid %d for search vaddr 0x%x and pid %d\n", i, coremap[i].vaddr, coremap[i].pid, vaddr, pid);
   
                splx(spl);
                
				//TODO: Update the tlb fault statistics
				if(pid == curthread->pid)
					total_tlb_faults++;
				else
					total_page_faults++;

                return coremap[i].paddr;
            }
	    else
		panic("invalid  paddress 0x%x\n 0x%x %d %d %d\n",vaddr,coremap[i].paddr,coremap[i].pid,curthread->pid, pid);
        }        
        //weird! address present but pid doesn't match! where did this page came
        //from, who allocated it? we don't know, so panic!
        if(coremap[i].vaddr == vaddr)
        {            
			// The core map should have multiple identical virtual addresses separated by pid so we need to keep searching
			//splx(spl);
            //panic("VM_PPAGE: address present but pid doesn't match 0x%x\n 0x%x %d %d %d\n",vaddr,coremap[i].paddr,coremap[i].pid,curthread->pid, pid);
        }
    }
    
    //Outside of the search loop, so the page doesn't present in memory. We must
    //bring the page from disk into memory. So, this is also a valid page fault
    
    //TODO: update page fault statistics
    total_page_faults++;
    splx(spl);

    DEBUG(DB_VM, "Searched for vaddr 0x%x and pid %d\n", vaddr, pid);
	DEBUG(DB_VM, "Couldn't find the page in memory.\n");

    /*
     * Bring back the page from disk by loading into memory. If memory is full
     * then replace a victim page to make space for this page. Return the paddr 
     * of the page
     */    
    paddr = load_page_into_memory(vaddr, pid);    
    
    return paddr ;    
}

/* 
 * This is the interface of our vm to handle tlb/page fault() by calling the
 * get_ppage() to bring the page into memory. It is responsible for updating 
 * the last access time of the page to make our LRU page replacement working.
 */
u_int32_t handle_page_fault(u_int32_t vaddr)
{
    u_int32_t paddr;
    
    //bring the page into memory if not present in memory and return the paddr
    //of this page
    paddr = get_ppage(vaddr & PAGE_FRAME, curthread->pid);
    assert(paddr!=0x0);
    
    /*
     * check whether the physical address is a valid 20 bit addr or not. If valid
     * then we are done, we now brought back the page successfully in the memory
     * so update the access time and return the paddr of the page so that 
     * vm_fault() can update the tlb table properly
     */
    if( IS_VALID(paddr) ) 
    {
	if(PAGE_REPLACEMENT_ALGO == LRU)
	{
		time_t sec; 
		u_int32_t nsec;
		gettime(&sec, &nsec);
		
		//update the access time
		int page_index = ((paddr & PAGE_FRAME)-coremap_base) / PAGE_SIZE;
		assert(page_index>=0 && page_index<(int)coremap_size);
		coremap[ page_index ].last_access_time_sec = (u_int32_t)sec;
		coremap[ page_index ].last_access_time_nsec = (u_int32_t)nsec;
	}
        
        return (paddr & PAGE_FRAME);
    }
       
    panic("VM: invalid page table fault 0x%x",vaddr);
    return 0;            
}

/*
 * alloc_page(): allocate a single page:
 * -------------------------------------
 * 1. Snatch a page from paging module by calling snatch_a_page()
 * 2. Insert the page into the coremap and mark the bitmap properly.
 * 3. Return the physical address of the page
 */
u_int32_t alloc_page(u_int32_t vaddr, int pid)
{
    u_int32_t paddr;
    
    //kprintf("vm bootstrap: alloc_page\n");
    
    //snatch a page from paging module. Paging module is responsible for all
    //paging/swapping mechanism to allocate the page
    paddr = snatch_a_page();
    
    //set valid attribute as snatch_a_page() guaranteed to return a valid paddr
    paddr = SET_VALID(paddr);
    //check whether it is a kernel page or not, if yes then mark as kernel and
    //we'll not touch it later
    if( vaddr >= MIPS_KSEG0)
        paddr = SET_KERNEL(paddr);
    //Add the invert entry for this page mapped to vaddr into pagetable coremap
    add_ppage(vaddr, paddr, pid, PAGE_DIRTY);
    
    return paddr;    
}

/* Allocate n contiguous kernel pages */
u_int32_t kpage_nalloc(int n, int pid)
{
    u_int32_t paddr;

    int spl=splhigh();
    int i;
    unsigned index=0,count=0;
    unsigned swap_index=0,swap_count=0;
    unsigned best_count=0,best_index=0;

    //kprintf("vm bootstrap: cp5\n");
    //Just one page? then snatch a page and update pagetable coremap
    if(n==1) 
    {
        u_int32_t paddr = snatch_a_page();
        paddr = SET_VALID(paddr);
        paddr = SET_KERNEL(paddr);
        add_ppage( PADDR_TO_KVADDR(paddr), paddr, curthread->pid, PAGE_DIRTY);
        splx(spl);
        
        return (paddr & PAGE_FRAME);
    }

    /* 
     * Otherwise, find the index of a hole from which there are at least n contagious pages. 
     */
    for(i = 0 ; i < (int)coremap_size; i++) 
    {
        //We should not replace kernel pages: kernel pages shouldn't be touched
        //so look for non-kernel pages to replace and make a big enough hole
	if(IS_KERNEL(coremap[i].paddr)) 
	{
            //If it is a kernel page and we didn't find a big enough hole so 
            //far then we can't take the current hole. So get back.
            swap_count = 0;
	}
	//a non-kernel page is found in the hole currently under investigation
        else 
        {
	    //ok last page was kernel an so we are restarting the hole from this page
            if( swap_count == 0) 
                swap_index = i;
            //keep maintaining how many contagious pages should be replaced
            swap_count++;
            
            //ok, now we have n contagious nonkernel pages. So, this is a potential good hole
            if ((int)swap_count >= n)
            {
		//update index and size of the best hole found so far
                if(best_count > count) 
                {
                    best_count = count;
                    best_index = swap_index;
                }

		//resrart the search from this page to find a better hole (if exists)
                swap_count = 0;
            }
        }
        
	//if this is a free page then a free hole can be started from here, set free hole index and size
        if( !bitmap_isset(core_memmap, i) )	       
        {
            index = i;
            count = 0;
        }
	else
	{
	    //ok, we can't get this page as free page. So, lets check if we have found n free pages or nor
	    //if we fund n pages then its the perfect free hole so far, so stop the search.
	    if((int)count == n) 
	    {
		break; 
	    }	               
	}

	count ++;
    }
    
    //so no of contagious free pages found in the search is not enough. 
    //So, we need to replace some non-kernel pages. Start from the best 
    //the best non-kernel hole we found in the search. 
    if((int)count < n) 
    {
        if((int)best_count < n)         
	{
	    //not enough pages to replace. Panic
            splx(spl);
            return 0; 
	}
	else
        {
            //ok, we have enough nonkernel pages to replace
            for(i=best_index; i < (int)(best_index + best_count); i++) 
            {
                u_int32_t ppaddr = coremap[i].paddr;
                //the page is valid
                if( IS_VALID(ppaddr) ) 
                {
                    //find an empty chunk in the disk to swap out the existing page
                    u_int32_t chunk = get_empty_chunk();
                    //mark entry into swap_table
                    splx(spl);
                    //Now, swapout the page into the chunk of the disk
                    swapout(chunk,ppaddr);
		    total_asyncpage_write++;
		    //update the swap area map to make the page frame free for allocation.
		    //we are allocating below. So, there is a possibility of a race condition here. 
                    remove_ppage(ppaddr);
                }
            }
	    
 	    //so, we have now replaced pages starting from best_index to make space for the demanded n pages.
	    //we will now claim these pages below.
            index = best_index;	
        }	
    }
        
    //claim the hole we set up above
    for(i=index ; i < (int)(index + n); i++) 
    {
        add_ppage(PADDR_TO_KVADDR(coremap[i].paddr), coremap[i].paddr, pid, PAGE_DIRTY);
    }
    splx(spl);    
    
    paddr = coremap[index].paddr;
    //kprintf("VM_ALLOCN: 0x%x\n", paddr);
    
    return (paddr & PAGE_FRAME);
}

#endif
