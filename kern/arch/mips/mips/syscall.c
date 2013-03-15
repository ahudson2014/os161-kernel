#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <clock.h>
#include <curthread.h>
#include <kern/stat.h>

#include "thread.h"


/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

void
mips_syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;
	time_t seconds; // Used in the gettime function
	u_int32_t nanoseconds; // Used in the gettime function
	int *timePtr;

	assert(curspl==0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;
	//DEBUG(DB_SYSCALL, "Handling system call #%u in syscall.c.\n", callno);
	switch (callno) {

   	    /* Add stuff here */

	    //added by tocurtis
		case SYS_open:
			//err = sys_open((char *)tf->tf_a0, tf->tf_a1, (char **)tf->tf_a2);
			err = sys_open((char *)tf->tf_a0, tf->tf_a1, tf->tf_a2, &retval);
			//char *path, int openflags, int mode
			break;

		//added by tocurtis
		case SYS_close:
			err = sys_close(tf->tf_a0, &retval);
			break;

		case SYS_read:
			err = sys_read(tf->tf_a0, (void *) tf->tf_a1, tf->tf_a2, &retval);
			//int sys_read(int fd, userptr_t buf, size_t size, int *retval)
			break;

		//added by tocurtis
		case SYS_fstat:
			err= sys_fstat(tf->tf_a0, (struct stat *) tf->tf_a1, &retval);
			//int sys_fstat(int fd, struct stat *statbuf, int *retval)
			break;

		case SYS_getdirentry:
			err= sys_getdirentry(tf->tf_a0, (char *) tf->tf_a1, tf->tf_a2, &retval);
			//int getdirentry(int fd, char *buf, size_t buflen); 
			break;

		case SYS_mkdir:
			err= sys_mkdir((char *)tf->tf_a0, tf->tf_a1, &retval);
			//int sys_mkdir(const char *pathname, int mode);
			break;

		case SYS_rmdir:
			err= sys_rmdir((char *)tf->tf_a0, &retval);
			//int sys_mkdir(const char *pathname, int mode);
			break;
		
		case SYS___getcwd:
			err= sys___getcwd((char*) tf->tf_a0, tf->tf_a1, &retval);
			break;

		case SYS_chdir:
			err= sys_chdir((char *)tf->tf_a0, &retval);
			//int sys_chdir(char *pathname, int *retval)
			break;

		case SYS_remove:
			err= sys_remove((char *)tf->tf_a0, &retval);
			//int vfs_remove(char *path)
			break;
		
		case SYS_rename:
			err= sys_rename((char *)tf->tf_a0, (char *)tf->tf_a1, &retval);
			//int vfs_rename(char *oldpath, char *newpath)
			break;

		case SYS_lseek:
			err = sys_lseek(tf->tf_a0, tf->tf_a1, tf->tf_a2, &retval);
			//int sys_lseek(int fd, off_t pos, int whence, int *retval)
			break;

		case SYS_fsync:
			err = sys_fsync(tf->tf_a0, &retval);
			//int fsync(int fd, int *retval)
			break;

		case SYS_dup2:
			err = sys_dup2(tf->tf_a0, tf->tf_a1, &retval);
			break;

		//added by rahmanmd
	    case SYS_fork:
	        ///kprintf("\nEntering sys_fork()\n");
	        err = sys_fork(tf, &retval);
	        //kprintf("\nreturned from sys_fork() %d\n", retval);
	        break;		

	    //added by rahmanmd	       
  	    case SYS_execv:
	        err=sys_execv((char *)tf->tf_a0,(char **)tf->tf_a1,&retval);
	        break;

	    //added by rahmanmd
	    case SYS_waitpid:
	        //curthread->priority = 0;
	        //kprintf("\nEntering waitpid\n");
	        err = sys_waitpid((pid_t)tf->tf_a0,(int *) tf->tf_a1, tf->tf_a2, &retval);
    	        //kprintf("\nreturned from pid %d\n", retval);
	        break;

	    //added by rahmanmd
	    case SYS_getpid:
                //kprintf("\nEntering getpid\n");
	        err = sys_getpid(&retval);
    	        break;
                
            case SYS_sbrk:
            //kprintf("\nEntering getpid\n");
            err = sys_sbrk(tf->tf_a0, &retval);
            break;

	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;	   
		
	    // Added by tocurtis
	    case SYS___time:
		DEBUG(DB_SYSCALL, "Getting the time..\n");
				
		gettime(&seconds, &nanoseconds);
		
		DEBUG(DB_SYSCALL, "Successfully retrieved the time.\n");
		DEBUG(DB_SYSCALL, "Seconds: %u.\n", (int) seconds);
		// Put the time into the return value
		// nanoseconds is a u_int32_t
		// seconds is a time_t
		// Not sure if we need convert to retval type
		retval = seconds;
		DEBUG(DB_SYSCALL, "Return Value: %u.\n", retval);
		// Move the value into the pointer address
		timePtr = tf->tf_a0;
		*timePtr = retval;
		err = 0; // gettime is void, and we made it to here
		break;
		
	    // Added by tocurtis
	    case SYS__exit:
		err = sys__exit(tf->tf_a0);
		break;

	    // Added by tocurtis
	    case SYS_write:
    		err = sys_write(tf->tf_a0, (void *) tf->tf_a1, tf->tf_a2, &retval);
		break;	
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);
}

        //added by rahmanmd
/*
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
//void
//md_forkentry(struct trapframe *trap, unsigned long pid)
//{	
//        /*
//         * before returning to user mode, we need to copy the modified trapframe 
//         * from kernel heap to stack, then use its address as parameter to call 
//         * mips_usermode,
//         */
//	struct trapframe *tf;
//	tf = (struct trapframe *) trap;		
//        
//        /*
//         * We need to set our return value in the copied trapframe to 0 since 
//         * we are the child which is done by modifying the v0 register. Also
//         * increment program counter to stop reentry of the same system call         
//         */
//	tf->tf_v0 = 0;
//	tf->tf_a0 = 0;
//	tf->tf_epc = tf->tf_epc + 4;
//	mips_usermode(tf); 
//}
    void
md_forkentry(struct trapframe *tf, unsigned long pid)
{
	/*
	 * This function is provided as a reminder. You need to write
	 * both it and the code that calls it.
	 *
	 * Thus, you can trash it and do things another way if you prefer.
	 */
        
        //added by rahmanmd
    
    	int s;
	s = splhigh();
      
        //kprintf("fork_entry child %l: \n", parent_addr_space);
        /*
         * before returning to user mode, we need to copy the modified trapframe 
         * from kernel heap to stack, then use its address as parameter to call 
         * mips_usermode,
         */
	struct trapframe trap;
	memcpy(&trap,tf,sizeof(struct trapframe));
	kfree(tf);

        /*
         * We need to set our return value in the copied trapframe to 0 since 
         * we are the child which is done by modifying the v0 register. Also
         * increment program counter to stop reentry of the same system call         
         */
	trap.tf_v0 = 0;
	trap.tf_epc += 4;
	trap.tf_a3 = 0;        
	splx(s);
                
        //kprintf("fork_entry child %l: returning to user mode\n", parent_addr_space);
	mips_usermode(&trap);
}
