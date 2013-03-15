
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <kern/unistd.h>
#include <syscall.h>
#include <thread.h>
#include <process.h>
#include <synch.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>
#include <curthread.h>
#include <kern/limits.h>
#include <machine/vm.h>
#include <vm.h>
#include <kern/stat.h>
#include <fs.h>

#include "addrspace.h"

int sys__exit(int code) 
{ 	
    //added by rahmanmd
    //report exit status	
    pid_t pid = curthread->pid; 
    struct process *parent = NULL;
    struct process* p = get_process(pid);
    if(p != NULL)          
    {        
        //acquire the exit lock to see whether the child has exited or not
        lock_acquire(p->exitlock);                        
        p->exited = 1;                                
        //(void)code;
        p->exitcode = code;        
        //sleep on the exitcode variable to be updated by the exiting child
        cv_broadcast(p->exitcv, p->exitlock);                       
        lock_release(p->exitlock);
    }
    
    //kprintf("\n Thread exitted %d\n", curthread->pid);
    thread_exit();

    panic("I shouldn't be here in sys__exit!");

    return 0;
}

/* This initializes our con for fd0, fd1, fd2 */
int init_con()
{
		
	int result;	
	char tty0[] = "con:";
	char tty1[] = "con:";
	char tty2[] = "con:";
	
	struct vnode *vn0;
	struct vnode *vn1;
	struct vnode *vn2;
	// Not taking any chances of having pointer issues!
		
	struct fdesc *f_desc0;
	struct fdesc *f_desc1;
	struct fdesc *f_desc2;
		
	f_desc0=(struct fdesc*)kmalloc(sizeof(struct fdesc));
	f_desc1=(struct fdesc*)kmalloc(sizeof(struct fdesc));
	f_desc2=(struct fdesc*)kmalloc(sizeof(struct fdesc));
		
	//kprintf("Setting up STDIN.\n");
	result = vfs_open(tty0, O_RDONLY, &vn0);
	if (result)
		return result;
	if (vn0->vn_fs != NULL)
		kprintf ("vn_fs != NULL!\n");
	fdesc_init(f_desc0, vn0);
	f_desc0->mode = 0664; 
	curthread->t_fdtable[0] = f_desc0;

	//kprintf("Setting up STDOUT:\n");
		
	result = vfs_open(tty1, O_WRONLY, &vn1);
	if (result)
		return result;
	if (vn0->vn_fs != NULL)
		kprintf ("vn_fs != NULL!\n");
	fdesc_init(f_desc1, vn1);	
	f_desc1->mode = 0664;
	curthread->t_fdtable[1] = f_desc1;

	
	//kprintf("Setting up STDERR.\n");
	result = vfs_open(tty2, O_WRONLY, &vn2);
	if (result)
		return result;
	if (vn0->vn_fs != NULL)
		kprintf ("vn_fs != NULL!\n");
	fdesc_init(f_desc2, vn2);	
	f_desc2->mode = 0664;
	curthread->t_fdtable[2] = f_desc2;
	
	//kprintf("Con set up.\n");
	
	return 0;

}

int sys_open(char *path, int openflags, int mode, int *retval)
{
	struct vnode *vn;
	struct fdesc *f_desc;
	
	// Turn off interrupts
	// Normally I will use the f_desc lock, but here we don't have
	// an f_desc until midway through. Since this should be a quick
	// operation, overall, turning off interrupts is not a major issue.
	int spl=splhigh();

	f_desc=(struct fdesc*)kmalloc(sizeof(struct fdesc));
	
	int fd;
	int i;
	size_t size;
	int result;
	
	assert(f_desc !=NULL);

	//kprintf("\nStaring open operation.\n");
	
	// Use vfs_open
	// Verify that program is a valid pointer
	
	/*kernel pointer to hold prog name*/
    //char *path_name = (char *)kmalloc(NAME_MAX+1);   
	char path_name[NAME_MAX+1];
	
	if(path == NULL)
		return EFAULT;
	/*
     * We don't believe in user supplied pointer for the path. Using 
     * copyinstr to check the validity and securely copy the progname from
     *  userspace into kernel space
     */
	copyinstr((userptr_t)path, path_name, NAME_MAX, &size);	
	
	// Verify that we have valid flags
	// We can only have one of the following
	if (((openflags & O_RDONLY) && (openflags & O_WRONLY))
		|| ((openflags & O_RDONLY) && (openflags & O_RDWR))
		|| ((openflags & O_WRONLY) && (openflags & O_RDWR)))
	{
		kprintf("Error: Conflicting file flags!\n");
		
		return EFAULT; // Need to find a better error code
	}
		
	// Check to see if we have set up our standard fd's for the console
	if (curthread->t_fdtable[0] == NULL)
	{
		result=init_con();
		if (result)
		{
			kprintf("Couldn't initialize con in open.\n");
			return result;
		}
	}	
	
	// Allocate a fd to the file by looking for an available slot in curthread->t_fdtable
	fd = -1;
	
	// fd 0,1,2 reserved
	for (i=3; i<MAX_OPENED_FILES; i++)
		if(curthread->t_fdtable[i] == NULL)
		{
			fd = i;
			break;
		}

	if (fd == -1)
	{
		// We have no available opening
		// Return an appropriate error code		
		kprintf("Could not allocate a file descriptor (fd)!\n");
		splx(spl);		
		return ENOSPC; // This is not exactly the issue
	}
	
	// Open file using vfs_open()
	result = vfs_open(path_name, openflags, &vn);
	
	if (result) 
	{
		kfree(f_desc);
		splx(spl);
        return result;
	}
    
	// Set up the file descriptor
	fdesc_init(f_desc, vn);	
	f_desc->mode = mode;
		
	//  Without O_APPEND, offset=0
	//  With O_APPEND, offset = file size (use VOP_STAT)
	if(openflags & O_APPEND)
	{
		struct stat *file_stat;
		//kprintf("Performing VOP_STAT.\n");
		VOP_STAT(vn, file_stat); // (&v,)?
		f_desc->offset = file_stat->st_size;
	}		
	else
	{	
		//kprintf("Setting offset to 0.\n");
		f_desc->offset = 0;
	}
		
	// Copy the file descriptor to the current thread
	curthread->t_fdtable[fd] = f_desc;
		
	// Restore interrupts
	splx(spl);

	*retval = fd;
	return 0;
}

int sys_close(int fd, int *retval)
{
	// Seems to be working correctly
	int spl;
	int result;
	
	spl=splhigh();
	// Use vfs_close().
	// Free the fdesc structure for the file.
	// Decrease its dup_count if it has been duplicated.
	
	//kprintf("Closing fd %d.\n", fd);
	
	vfs_close(curthread->t_fdtable[fd]->vn);
	
	// Decrease the dup count
	curthread->t_fdtable[fd]->dup_count--;
		
	// If there are no more references, remove the fd from the table
	if(curthread->t_fdtable[fd]->dup_count < 0)
	{
		curthread->t_fdtable[fd]=NULL;
	}
	else
	if(curthread->t_fdtable[fd]->vn != NULL && curthread->t_fdtable[fd]->vn->vn_refcount <= 0) {		
		//kfree(curthread->t_fdtable[fd]->vn);
		//kfree(curthread->t_fdtable[fd]);
		curthread->t_fdtable[fd]=NULL;
		curthread->t_fdtable[fd]->vn = NULL;
	}
	
	splx(spl);
	//kprintf("Closed.\n");
	return 0;
}

int sys_write(int fd, void* buf, size_t size, int *retval)  
{
	// Tested working with testbin/filetest
	
	int result;	
	struct vnode *vn;
	struct uio u;
		
	struct fdesc *f_desc;
	
	//f_desc=(struct fdesc*)kmalloc(sizeof(struct fdesc));
	
	//(void) fd; // All writes go to "con:" for now
	//DEBUG(DB_SYSCALL, "Opening vfs.\n");

	// Check to see if we have set up our standard fd's
	if (curthread->t_fdtable[0] == NULL)
	{
		result=init_con();
		//kprintf("Initializing con.\n");
		if (result)
		{
			kprintf("Couldn't initialize con in write.\n");
			return result;
		}
	}
	//kprintf("Starting write to fd %d.\n", fd);

	if (curthread->t_fdtable[fd] == NULL)
	{
		kprintf("Invalid fd in write!");
		return -1;
	}

	f_desc = curthread->t_fdtable[fd];

	// Get the lock
	lock_acquire(f_desc->f_lock);

	char *kbuf = (char *)kmalloc(size);
	// Use copyin since we don't trust the supplied buffer
	if ((result = copyin((userptr_t)buf, kbuf, size)) != 0) {
			kfree(kbuf);
			lock_release(f_desc->f_lock);	
			return result;
	}
	
	mk_kuio(&u, (void*) kbuf, size, f_desc->offset, UIO_WRITE);
	
	//DEBUG(DB_SYSCALL, "Attempting to write.\n");
				
	result = VOP_WRITE(f_desc->vn, &u);	
	kfree(kbuf);

	if(result != 0)
	{
		kprintf("Error in VOP_WRITE inside sys_write!\n");
		lock_release(f_desc->f_lock);	
		return result;
	}
	
	// Update the offset? 
	//f_desc->offset = u.uio_offset;

	// Release the lock
	lock_release(f_desc->f_lock);	

	//DEBUG(DB_SYSCALL, "Done writing.\n");
	*retval = size - u.uio_resid;
	
	return 0;
}

int sys_read(int fd, void* buf, size_t size, int *retval)
{
	// Tested working with testbin/filetest

	//struct iovec iov;
	struct uio u;	
	struct fdesc *f_desc;
	int result;

	f_desc = curthread->t_fdtable[fd];
	
	// Check to make sure the fdesc is valid
	if(f_desc == NULL)
		return EBADF;
	
	// Check to see if we have set up our standard fd's
	// Not sure if we need to do this here or just in write
	if (curthread->t_fdtable[0] == NULL)
	{
		result=init_con();
		if (result)
		{
			kprintf("Couldn't initialize con in read.\n");
			kfree(f_desc);
			return result;
		}
	}

	char *kbuf = (char *)kmalloc(size);
		
	// Get the lock
	lock_acquire(f_desc->f_lock);

	mk_kuio(&u, (void*) kbuf, size, f_desc->offset, UIO_READ);

	
	result = VOP_READ(f_desc->vn, &u);
	
	if(result != 0)
	{
		kfree(kbuf);
		kprintf("Error in VOP_READ inside sys_read!\n");
		return result;
	}
		
	// Update the offset? 
	// See same problem above in write
	//f_desc->offset = u.uio_offset;

	copyout((const void *)kbuf, (userptr_t)buf, size);

	kfree(kbuf);
	
	// Release the lock
	lock_release(f_desc->f_lock);	

	*retval = size - u.uio_resid;
	
	return 0;	
}

int sys_fstat(int fd, struct stat *statbuf, int *retval)
{
	//fstat retrieves status information about the file referred to by the file handle fd
	// and stores it in the stat structure pointed to by statbuf.
	struct vnode *vn;
		
	//kprintf("Checking stats.\n");
	// Check for a valid fd
	if (curthread->t_fdtable[fd] == NULL)
	{
		*retval = -1;
		return EBADF;
	}
	
	vn = curthread->t_fdtable[fd]->vn;
	VOP_STAT(vn, statbuf);
	//kprintf("Stat, mode=%d.\n", statbuf->st_mode);
	//kprintf("Returning stats.\n");
		
	return 0;
}

int sys_getdirentry(int fd, void* buf, size_t buflen, int *retval)
{
	struct vnode *vn;
	struct uio u;
	//struct iovec iov;
	int result;

	// Check for a valid fd
	if (curthread->t_fdtable[fd] == NULL)
	{
		*retval = -1;
		return EBADF;
	}
	//kprintf("Getting directory entry at fd=%d.\n", fd);
	vn = curthread->t_fdtable[fd]->vn;
	
	//char kbuf[buflen];
	char *kbuf = (char *)kmalloc(buflen);

	mk_kuio(&u, (void*) kbuf, buflen, 0, UIO_READ);

	int spl = splhigh();
	result = VOP_GETDIRENTRY(vn, &u);
	splx(spl);	

	if(result)
	{
		kfree(kbuf);
		return ENOTDIR;
	}
	
	copyout((const void *)kbuf, (userptr_t)buf, buflen);
	//kprintf("Returning directory entry.\n");

	// What do we set retval to?
	// It is set to the length of the filename by definition
	
	// Always moves 1 past the end of name (it seems...)
	// This may need to be changed when rest is working correctly
	*retval = result;//(buflen - u.uio_resid) - 1; 
	
	kfree(kbuf);
	
	return 0;
}

int sys_mkdir(char *pathname, int mode, int *retval)
{
	int result;
	size_t size;
	
	kprintf("Making directory %s with mode %u.\n", pathname, mode);
	
	// Check to see if name already exists
	// TODO
	
	/*kernel pointer to hold prog name*/
    char *path_name = (char *)kmalloc(PATH_MAX);   
	
	if(pathname == NULL)
        return EFAULT;
    
	/*
     * We don't believe in user supplied pointer for the path. Using 
     * copyinstr to check the validity and securely copy the progname from
     *  userspace into kernel space
     */    

    copyinstr((userptr_t)pathname, path_name, PATH_MAX, &size);	
	
	result = vfs_mkdir(path_name);
	
	if (result)
	{
		kfree(path_name);
		kprintf("Could not make directory!\n");
		*retval = -1;
		return result;
	}
	
	kfree(path_name);
	kprintf("Directory made.\n");
	return 0;
}

int sys_rmdir(char *pathname, int *retval)
{
	int result;
	size_t size;
	kprintf("Removing directory %s.\n", pathname);
	
	/*kernel pointer to hold prog name*/
        char *path_name = (char *)kmalloc(PATH_MAX);   
	
	if(pathname == NULL)
        return EFAULT;

	// Use copyinstr for safety
    copyinstr((userptr_t)pathname, path_name, PATH_MAX, &size);	

	result = vfs_rmdir(path_name);
	if (result)
	{
		kfree(path_name);
		kprintf("Could not remove directory!\n");
		*retval = -1;
		return result;
	}

	kfree(path_name);
	kprintf("Directory removed.\n");
	return 0;
}

//int sys___getcwd(userptr_t buf, size_t buflen, int *retval)
int sys___getcwd(void* buf, size_t buflen, int *retval)
{
	char *name = (char*)kmalloc(buflen);

	int result;
	struct uio u;
	
	mk_kuio(&u, (void*) name, buflen, 0, UIO_READ);
	result = vfs_getcwd(&u);
	
	if (result)
	{
		kprintf("Could not get working directory!\n");
		*retval = -1;
		return result;
	}
	
	name[buflen-1-u.uio_resid] = 0;
	size_t size;
	copyoutstr((const void *)name, (userptr_t)buf, buflen, &size);

	*retval = buflen - u.uio_resid;
	//kprintf("Retrieved working directory.\n");
	kfree(name);
	
	return 0;
	
}

int sys_chdir(char *pathname, int *retval)
{
	int result;
	size_t size;
	kprintf("Changing path to %s.\n", pathname);
	
	/*kernel pointer to hold prog name*/
    char *path_name = (char *)kmalloc(PATH_MAX);   
	
	if(pathname == NULL)
        return EFAULT;
	
	// Use copyinstr for safety 
	copyinstr((userptr_t)pathname, path_name, PATH_MAX, &size);	

	result = vfs_chdir(path_name);
	if (result)
	{
		kfree(path_name);
		kprintf("Could not change path!\n");
		*retval = -1;
		return result;
	}

	kfree(path_name);
	kprintf("Path changed.\n");
	return 0;
}

int sys_remove(char *pathname, int *retval)
{
	int result;
	size_t size;
	int spl;

	spl=splhigh();
	
	kprintf("Removing file %s.\n", pathname);
	
	/*kernel pointer to hold prog name*/
    char *path_name = (char *)kmalloc(PATH_MAX);   
	
	if(pathname == NULL)
        return EFAULT;

	// Use copyinstr for safety
    copyinstr((userptr_t)pathname, path_name, PATH_MAX, &size);	
		
	result = vfs_remove(path_name);
				
	if (result)
	{
		kfree(path_name);
		kprintf("Could not remove file!\n");
		*retval = -1;
		return result;
	}

	kfree(path_name);
	
	kprintf("File removed.\n");
	splx(spl);
	return 0;
}

int sys_rename(char *old_pathname, char *new_pathname, int *retval)
{
	int result;
	size_t size;
	kprintf("Renaming file %s to %s.\n", old_pathname, new_pathname);
	
	/*kernel pointer to hold prog name*/
    char *old_path_name = (char *)kmalloc(PATH_MAX);   
	char *new_path_name = (char *)kmalloc(PATH_MAX); 
	
	if(old_pathname == NULL || new_pathname == NULL)
        return EFAULT;

	// Use copyinstr for safety
    copyinstr((userptr_t)old_pathname, old_path_name, PATH_MAX, &size);	
	copyinstr((userptr_t)new_pathname, new_path_name, PATH_MAX, &size);	

	result = vfs_rename(old_path_name, new_path_name);
	if (result)
	{
		kfree(old_pathname);
		kfree(new_pathname);
		kprintf("Could not rename file!\n");
		*retval = -1;
		return result;
	}

		kfree(old_pathname);
		kfree(new_pathname);
	kprintf("File renamed.\n");
	return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval)
{
	int result;
	size_t size;
	struct fdesc *f_desc = curthread->t_fdtable[oldfd];

	kprintf("Duplicating fd (dup2).\n");
	
	// Make sure newfd is closed first
		
	if (curthread->t_fdtable[newfd] != NULL)
		result = sys_close(newfd, retval);
	
	if(result)
	{
		kprintf("Error closing an already open newfd in dup2!\n");
		*retval = -1;
		return result;
	}
	 
	// Make sure that oldfd and newfd are valid (>0 and <MAX_OPENED_FILES)
	if (oldfd < 0 || newfd < 0)
	{
		kprintf("Dup2 was given a fd < 0!\n");
		*retval = -1;
		return EBADF;
	}

	if (newfd >= MAX_OPENED_FILES)
	{
		kprintf("Dup2 given a newfd > MAX_OPENED_FILES!/n");
		*retval = -1;
		return EMFILE;
	}
	
	/*
	After dup2, oldfd and newfd points to the same file. But we can call close on any of them and do not influence the other.
	After dup2, all read/write to newfd will be actually performed on oldfd. (Of course, they points to the same file!!)
	If newfd is previous opened, it should be closed in dup2 ( according to dup2 man page)
	Maintain the fdesc->dup_count.
	*/

	// Get the lock
	//lock_acquire(f_desc->f_lock);

	// Copy over the relevant information
	
	// Increase the dup_count for the current thread
	curthread->t_fdtable[oldfd]->dup_count++;
		
	strcpy(curthread->t_fdtable[newfd]->name, curthread->t_fdtable[oldfd]->name);
	
	curthread->t_fdtable[newfd]->mode = curthread->t_fdtable[oldfd]->mode;
	curthread->t_fdtable[newfd]->offset = curthread->t_fdtable[oldfd]->offset;
	curthread->t_fdtable[newfd]->dup_count = curthread->t_fdtable[oldfd]->dup_count;
	curthread->t_fdtable[newfd]->offset = curthread->t_fdtable[oldfd]->offset;
	curthread->t_fdtable[newfd]->vn = curthread->t_fdtable[oldfd]->vn;
	curthread->t_fdtable[newfd]->f_lock = curthread->t_fdtable[oldfd]->f_lock;

	// Release the lock
	//lock_release(f_desc->f_lock);

	kprintf("Dup2 successful.\n");
	return newfd;
	
}

int sys_lseek(int fd, off_t pos, int whence, int *retval)
{
	/*
	If whence is 
	SEEK_SET, the new position is pos. 
	SEEK_CUR, the new position is the current position plus pos. 
	SEEK_END, the new position is the position of end-of-file plus pos. 
		anything else, lseek fails. 
	*/
		
	struct vnode *vn;
		
	kprintf("Trying seek.\n");
	// Check for a valid fd
	if (curthread->t_fdtable[fd] == NULL)
	{
		*retval = -1;
		return EBADF;
	}
	
	if(whence == SEEK_CUR)
	{
		// Add current position to pos
		// Where is current position? The offset?
		pos += curthread->t_fdtable[fd]->offset;
	}
	
	if(whence == SEEK_END)
	{
		struct stat *file_stat;
		kprintf("Performing VOP_STAT.\n");
		VOP_STAT(vn, file_stat); // (&vn)?
		pos += file_stat->st_size;
	}
	
	vn = curthread->t_fdtable[fd]->vn;
	VOP_TRYSEEK(vn, pos);

	kprintf("Done with seek.\n");
	return 0;
}

int sys_fsync(int fd, int *retval)
{
	struct vnode *vn;
		
	kprintf("Trying fsync.\n");
	// Check for a valid fd
	if (curthread->t_fdtable[fd] == NULL)
	{
		*retval = -1;
		return EBADF;
	}
	
	vn = curthread->t_fdtable[fd]->vn;
	VOP_FSYNC(vn);

	kprintf("Done with fsync.\n");
	return 0;

}

/*
 * added by rahmanmd
 * 
 * getpid() system call implementation
 */

int sys_getpid(int* ret)
{     
    *ret = curthread->pid;    
    //getpid doesn't fail
    return 0;
}

/*
 * added by rahmanmd
 * 
 * fork() system call implementation
 */
int sys_fork(struct trapframe* tf, int* ret)
{
    // TODO: Copy the thread's t_fdtable[] to the child!
	
	int error=0;    
    pid_t id = -1;
    sys_getpid(&id);
    struct thread *child_thread = NULL;
    //kprintf("sys_fork by parent thread %d\n", id);
    
    //Make a copy of the parent's trap frame on the kernel heap
    struct trapframe *parent_tf_copy = kmalloc(sizeof(struct trapframe));
    //memcpy(&newthread->t_stack[16], tf, (sizeof(struct trapframe)));

    if (parent_tf_copy == NULL) {            
            kfree(parent_tf_copy);
            return ENOMEM;
    }

    int s;
    s = splhigh();
    memcpy(parent_tf_copy,tf,sizeof(struct trapframe));
    splx(s);

    /*
     * call thread_fork, passing with the kernel buffered trapframe, 
     * and the address space. 
     * 
     * thread_fork will responsible for assigning a pid to this thread and 
     * return it. thread_fork also make sure to make an entry for the new
     * thread by calling fork_entry
     */        
    error = thread_fork(curthread->t_name, (void*)parent_tf_copy, 0, md_forkentry, &child_thread);

    //Return the child's process id
    *ret = child_thread->pid;    

    return error;
}


/*
 * Added by rahmanmd
 * execv() system call. Most of the codes are taken from userprogram/runprogram.c
 */
int
sys_execv(const char *progname, char **args, int *retval)
{
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result, i = 0;
    size_t arglen, size;;

    /*
     * Kernel buffer to hold arguments
     */
    int kargc;
    char **kargv;
    /*kernel poiter to hold prog name*/
    char *prog_name = (char *)kmalloc(PATH_MAX);    

    /*Counting arguments*/
    while(args[i] != NULL)
            i++;
    kargc = i;
    
    /*
     * We don't believe in user supplied pointer for the program name. Using 
     * copyinstr to check the validity and securely copy the progname from
     *  userspace into kernel space
     */
    if(progname == NULL)
    {
        *retval = -1;
        return EFAULT;
    }		
    copyinstr((userptr_t)progname,prog_name,PATH_MAX,&size);	

    //allocating kernel buffer to hold arguments
    kargv = (char **)kmalloc(sizeof(char*));
    
    /*
    * We don't believe in user supplied pointer for the program name. Using 
    * copyinstr to check the validity and securely copy user level arg buffers
    * from userspace into kernel space
    */    	
    for(i = 0; i < kargc; i++) 
    {
        int len = strlen(args[i])+1;        
        kargv[i]=(char*)kmalloc(len);
        copyinstr((userptr_t)args[i], kargv[i], len, &arglen);
    }
    //Null terminate kargv
    kargv[kargc] = NULL;

    /* Open the executable file. */
    result = vfs_open(prog_name, O_RDONLY, &v);
    if (result) 
    {
        *retval = -1;
        return result;
    }
    //destroy the address space to load new executable
    if(curthread->t_vmspace != NULL)
    {
        as_destroy(curthread->t_vmspace);
        curthread->t_vmspace=NULL;
    }
    /* We should be a new thread. */
    assert(curthread->t_vmspace == NULL);

    /* Create a new address space. */
    curthread->t_vmspace = as_create();
    if (curthread->t_vmspace==NULL) 
    {
        vfs_close(v);
        *retval = -1;
        return ENOMEM;
    }

    /* Activate it. */
    as_activate(curthread->t_vmspace);

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) 
    {            
        vfs_close(v);
        *retval = -1;
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(curthread->t_vmspace, &stackptr);
    if (result)
    {        
        *retval = -1;
        return result;
    }

    /*           
     * Argument pointers must be aligned by 4. So, do padding where 
     * necessary. Also we need to be careful to access stack as stack 
     * grows from top to bottom. That is we need to copy the last 
     * argument (properly aligned) in the top of the stack. Then the 
     * stack top will shrink.
     */
    unsigned int parameter_stack[kargc];	
    for(i = kargc-1; i >= 0; i--) 
    {
        int length = strlen(kargv[i]);
        
        int packed_length = (length%4);
        if(packed_length > 0)
            packed_length = ((int)(length/4)+1)*4;    
        else if(packed_length == 0)        
            packed_length = length;
        //
        stackptr = stackptr - (packed_length);
        copyoutstr(kargv[i], (userptr_t)stackptr, length, &arglen);
        parameter_stack[i] = stackptr;
    }

    /*
    * Copy the user arguments from kernel buffers into user stack, 
    * because it’s the only space we know for sure. First using copyout
    * to copy the buffer into user stack
    */
    parameter_stack[kargc] = (int)NULL;
    for(i = kargc-1; i >= 0; i--)
    {
        stackptr = stackptr - 4;
        copyout(&parameter_stack[i] ,(userptr_t)stackptr, sizeof(parameter_stack[i]));
    }

    *retval = 0;	
    kfree(kargv);
    /* Warp to user mode. */  
    //kprintf("checkpoint1\n");
    md_usermode(kargc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/, stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");

    return EINVAL;
}


int sys_waitpid(pid_t pid, int *status, int options, int *retval)
{
    (void) options;
    int stat, result;
    
    //index into process table
    struct process* p = get_process(pid);
    if(p == NULL)          
    {
        *retval = -1;
        return EINVAL;
    }    
    
    //acquire the exit lock
    lock_acquire(p->exitlock);
    pid_t my_pid = curthread->pid;
    
    /*
     * Copy user supplied status pointer using copyin 
     */
    result = copyin((userptr_t)status,&stat,sizeof(status));
    
    //kprintf("checkpoint3\n");
    //no one is waiting for this thread
    if(result && curthread->ppid != 0)
    {
        lock_release(p->exitlock);
        *retval = -1;
        return result;
    }

    //kprintf("checkpoint4\n");
    //is pid pur child? should we wait?
    if(!pid_exists(pid) || pid <= my_pid)
    {
        lock_release(p->exitlock);
        *retval = -1;
        return EINVAL;
    }

    //kprintf("checkpoint5\n");
    //status is not valid
    if(status == NULL)
    {
        lock_release(p->exitlock);
        *retval = -1;
        return EINVAL;
    }
        
    //kprintf("checkpoint6\n");
    //WNOHANG option, return 0 immediately 
    if(options == 1)
    {                
        *retval = 0;
        lock_release(p->exitlock);
        return 0;
    }
    
    //kprintf("checkpoint7\n");
    //we can't handle other options
    if(options > 1)
    {                
        *retval = -1;
        lock_release(p->exitlock);
        return EINVAL;
    }
    
    //kprintf("checkpoint8\n");
    //wait for the child to report exit status
    cv_wait(p->exitcv, p->exitlock);
    
    *status = p->exitcode;
   
    //kprintf("checkpoint8\n");      
    *retval = pid;
        
    lock_release(p->exitlock);
        
    //free the pid slot  
    remove_process(pid);	    
    return 0;
}

//added by rahmanmd
#if OPT_DUMBVM
int sys_sbrk(int size, int *retval)
{
    *retval=-1;
    return 1;
}
#else
int sys_sbrk(int size, int *retval)
{
    struct addrspace *addrsp;
    vaddr_t heaptop;
    size_t pages;
    u_int32_t i;
    
    //save the current heaptop
    addrsp = curthread->t_vmspace;        
    heaptop=addrsp->as_heaptop;
    
    //sanity checks, if size is zero return the heaptop
    if(size==0)
    {
        *retval = heaptop;
        return 0;
    }
    //size can't be less than zero
    if(size<0) 
    {
        *retval = -1;
        return EINVAL;
    }
    
    pages=(size/PAGE_SIZE)+1;

    //allocated size can't exceed the heap limit (i.e. can't fall outside its stackspace)
    if( (addrsp->as_heaptop+(pages*PAGE_SIZE)) > (USERSTACK+(VM_STACKPAGES*PAGE_SIZE)) ) 
    {
        *retval = -1;
        return EINVAL;
    }

    //allocate no_of_pages pages by calling each using allocate_page to allocate
    //single page. Allocation of each page followed by adding page mapping of 
    //vaddr to paddr in the page table. alloc_page() will be responsible to call
    //for this mapping. Note that the virtual address space is contagious but 
    //physical pages are not necessarily contagious
    for(i=0;i<pages;i++)
    {
        paddr_t res;
        if(mips_vm_enabled == 0)
            res=getppages(1);			
        else
            res=alloc_page(addrsp->as_heaptop+(i*PAGE_SIZE),addrsp->pid);		    
        if(res == 0) 
        {
            *retval = -1;
            return ENOMEM;		
        }
    }
    
    //increment the heaptop by the size of the total allocation
    addrsp->as_heaptop=addrsp->as_heaptop+(pages*PAGE_SIZE);
    
    //return the heaptop
    *retval = heaptop;

    return 0;
}
#endif
