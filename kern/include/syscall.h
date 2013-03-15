#ifndef _SYSCALL_H_
#define _SYSCALL_H_


/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);
int sys__exit(int code);
int init_con();
//int sys_write(int fd, userptr_t buf, size_t size, int *retval);
//int sys_read(int fd, userptr_t buf, size_t size, int *retval);
int sys_write(int fd, void* buf, size_t size, int *retval);
int sys_read(int fd, void* buf, size_t size, int *retval);
int sys_fork(struct trapframe* tf, int *ret);
int sys_getpid(int *ret);
int sys_execv(const char *progname, char **args, int *retval);
int sys_waitpid(pid_t pid, int *status, int options,int *retval);
int sys_sbrk(int size, int *ret);
int sys_open(char *path, int openflags, int mode, int *retval);
int sys_close(int fd, int *retval);
//int sys_fstat(int fd, struct stat *statbuf, int *retval);
//int sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *retval);
int sys_getdirentry(int fd, void* buf, size_t buflen, int *retval);
int sys_mkdir(char *pathname, int mode, int *retval);
int sys_rmdir(char *pathname, int *retval);
//int sys___getcwd(userptr_t buf, size_t buflen, int *retval);
int sys___getcwd(void* buf, size_t buflen, int *retval);
//int sys___getcwd(char *fname, size_t buflen, int *retval);
int sys_chdir(char *pathname, int *retval);
int sys_lseek(int fd, off_t pos, int whence, int *retval);
int sys_remove(char *pathname, int *retval);
int sys_rename(char *old_pathname, char *new_pathname, int *retval);
int sys_fsync(int fd, int *retval);
int sys_dup2(int oldfd, int newfd, int *retval);

#endif /* _SYSCALL_H_ */
