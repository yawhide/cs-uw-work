#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <vm.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <test.h>
#include <limits.h>

int
sys_execv(const_userptr_t prog, userptr_t* args){
	int err;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int argc = 0;
	char **argv;
	int totalStringSize = 0, totalPtrSize = 0;
	size_t proglength = 0;
	char * progname = kmalloc(PATH_MAX*sizeof(char));

	argv = kmalloc(64*sizeof(char *));
	err = copyin((userptr_t)args, argv, 64);
	if(err)	return err;

	while(argv[argc] != NULL){
		char* str = kmalloc(PATH_MAX*sizeof(char));
		size_t len;
		err = copyinstr((const_userptr_t)argv[argc], str, PATH_MAX, &len);
		if(err) return err;
		argv[argc] = str;
		totalStringSize = totalStringSize + strlen(argv[argc]) + 1;
		argc++;
	}
	
	err = copyinstr(prog, progname, PATH_MAX, &proglength);
	if(err) return err;
	argc++;

	totalStringSize = ROUNDUP(totalStringSize+strlen(progname) +1, 8);
	totalPtrSize = ROUNDUP((argc+1)*4, 8);
	//kprintf("sizes for stack: %d, %d\n", totalStringSize, totalPtrSize);
	//kprintf("progname: %s %d, argc:%d\n", progname, strlen(progname), (int)argc);

	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}
	curproc_setas(as);
	as_activate();
	result = load_elf(v, &entrypoint);
	if (result) {
		vfs_close(v);
		return result;
	}
	vfs_close(v);

/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	int acc = 0;
	vaddr_t *stringAddrs = kmalloc((argc+1) * sizeof(vaddr_t));
	vaddr_t stackaddr = stackptr;

	stackaddr = stackptr - totalStringSize;

	for (int i = 0; i < argc; ++i){
		size_t len;
		if(i == 0){
			err = copyoutstr(progname, (userptr_t)stackaddr, strlen(progname)+1, &len);
			if(err) return err;
			stackaddr += (strlen(progname) + 1);
		} else {
			err = copyoutstr(argv[i-1], (userptr_t)stackaddr, strlen(argv[i-1])+1, &len);
			if(err) return err;
			stackaddr += (strlen(argv[i-1])+1);
		}
	}
	stackaddr = stackptr - totalStringSize;

	for(int i = 0; i < argc+1; i++){
		if(i == argc){
			stringAddrs[i] = (vaddr_t)NULL;
		} else {
			stringAddrs[i] = stackaddr + acc;
			if(i == 0)
				acc = acc + strlen(progname) + 1;
			else
				acc = acc + strlen(argv[i-1]) +1;
		}
	}

	stackaddr = stackptr - totalStringSize - totalPtrSize;
	for (int i = 0; i < argc+1; i++){
		err = copyout(&stringAddrs[i], (userptr_t)stackaddr, sizeof(vaddr_t));
		if(err) return err;
		stackaddr += 4;
	}

	stackaddr = stackptr - totalStringSize;
	stackptr = stackptr - totalStringSize - totalPtrSize;

//curproc_setas(NULL);

/* Warp to user mode. */
	//kprintf("argc: %d, stackaddr: %p, stackptr: %p\n", (int)argc, (void *)stackaddr, (void *)stackptr);

	enter_new_process(argc+1, (userptr_t)stackptr, stackptr, entrypoint);

/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void 
sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  lock_acquire(pid_lock);
  getProcData(curproc->pid)->exitcode = exitcode;
  cv_signal(getProcData(getProcData(curproc->pid)->parent)->cv, pid_lock);
  lock_release(pid_lock);

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;
  int childExitcode;


  if (options != 0) {
    return(EINVAL);
  }
  // critical section
  lock_acquire(pid_lock);

  if(getProcData(pid) == NULL)
    return(ESRCH);

  if(getProcData(pid)->parent != curproc->pid)
    return(ECHILD);

  while((childExitcode = getProcData(pid)->exitcode) == -2){
    cv_wait(getProcData(curproc->pid)->cv, pid_lock);
  }
  lock_release(pid_lock);

  exitstatus = _MKWAIT_EXIT(childExitcode);

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  KASSERT(curproc->pid != -1);
  *retval = curproc->pid;
  return(0);
}

int
sys_fork(
    struct trapframe *tf,
    pid_t *retval
  )
{
  int err, as_copy_err, genPidErr;
  struct proc *child_proc;
  struct trapframe *child_tf;
  

  child_proc = proc_create_runprogram("child_proc");
  if(child_proc == NULL)
    return (ENOMEM);
  
  child_tf = kmalloc(sizeof(struct trapframe));
  if(child_tf == NULL){
    return (ENOMEM);
  }
  memcpy(child_tf, tf, sizeof(struct trapframe));

  as_copy_err = as_copy(curproc->p_addrspace, 
        &child_proc->p_addrspace);
  if(as_copy_err == ENOMEM){
    proc_destroy(child_proc);
    *retval = -1;
    return (ENOMEM);
  }

  // special adam process case
  if(curproc->pid == 2)
    generatePidForAdam();

  genPidErr = generatePid(child_proc);
  if(genPidErr == -1){
    return(ENPROC);
  }

  err = thread_fork(child_proc->p_name, child_proc, 
      (void *)enter_forked_process,
      child_tf,
      (unsigned long) 1);
  if(err){
    proc_destroy(child_proc);
  }

  *retval = child_proc->pid;
  return(0);
}


