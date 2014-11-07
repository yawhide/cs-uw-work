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
  // const char * program, char ** args
  int err;

  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;
  int argc = 0;
  char **argv, **kargv;
  //char *progname;
  //size_t len;

  // copyinstr(prog, progname, PATH_MAX, &len);
  // kprintf("hi\n");
  // KASSERT(progname != NULL);
  // kprintf("%s\n", progname);

 // kprintf("prog is: %s\n", (char*)prog);
  argv = kmalloc(64*sizeof(char *));
  err = copyin((userptr_t)args, argv, 64);
  if(err)
    return err;

  while(argv[argc] != NULL){
    char* str = kmalloc(PATH_MAX*sizeof(char));
    size_t len;
    err = copyinstr((userptr_t)argv[argc], str, PATH_MAX, &len);
    if(err)
      return err;
    argv[argc] = str;
    kprintf("len: %d, %s\n", len, argv[argc]);
    argc++;
  }
  kargv = kmalloc((argc+1)*sizeof(char *));
  kargv[argc] = NULL;
  for(int i = 0; i < argc; i++){
    int strLength = strlen(argv[i]);
    int roundedLen = ROUNDUP(strLength+1, 4);
    kargv[i] = kmalloc(roundedLen*sizeof(char));
    strcpy(kargv[i], argv[i]);
    // pad null terminators
    for(int j = strLength+1; j < roundedLen; j++)
      kargv[i][j+1] = '\0';
    kfree(argv[i]);
  }
  kfree(argv);

  kprintf("argc is: %d\n", argc);
  kprintf("soooooooo %s\n", kargv[0]);


  char *fname_temp;
  fname_temp = kstrdup((char*)prog);
  result = vfs_open(fname_temp, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }
  kfree(fname_temp);
  as = as_create();
  if (as ==NULL) {
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

  // part I have to do


  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }

  copyout(kargv, &stackptr, (argc+1)*sizeof(char *));
  for(int i = 0; i < argc; i++){
    int len;
    int strLength = ROUNDUP(strlen(kargv[i]), 4);
    copyoutstr(kargv[i], &kargv[i], strLength, &len);
  }
  

  /* Warp to user mode. */
  enter_new_process(argc, kargv,
        stackptr, entrypoint);
  
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


