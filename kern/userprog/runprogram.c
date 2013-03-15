/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>

int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		return result;
	}

	/* Warp to user mode. */
	md_usermode(0 /*argc*/, NULL /*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

int
runprogram_args(char *progname, int argc, char **args)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result, i;
    size_t buflen = 0;

    char **argv = kmalloc(sizeof(char*) * argc);
    size_t *offsets = kmalloc(sizeof(size_t) * argc);
	//kprintf("We have %d args in runprogram_args.\n", argc);
    for (i = 0; i < argc; ++i) {
        size_t len = strlen(args[i]) + 1;
        argv[i] = kstrdup(args[i]);
        offsets[i] = buflen;
        buflen += len;
    }

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		return result;
	}

    /* Copy arguments to stack */
	userptr_t argbase, userargv, arg, argv_base;
	vaddr_t stack = stackptr - buflen;
	stack -= (stack & (sizeof(void *) - 1));
	argbase = (userptr_t)stack;

    for (i = 0; i < argc; ++i) {
        result = copyout(argv[i], argbase + offsets[i], strlen(argv[i]) + 1);
        if (result)
            return result;
    }

	stack -= (argc + 1) * sizeof(userptr_t);
	userargv = (userptr_t)stack;
	argv_base = (userptr_t)stack;

	for (i = 0; i < argc; i++) {
		arg = argbase + offsets[i];
		result = copyout(&arg, userargv, sizeof(userptr_t));
		if (result)
			return result;
		userargv += sizeof(userptr_t);
	}

	arg = NULL;
	result = copyout(&arg, userargv, sizeof(userptr_t));
	if (result)
		return result;

	stackptr = stack + sizeof(userptr_t);

	/* Warp to user mode. */
	md_usermode(argc, argv_base,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

