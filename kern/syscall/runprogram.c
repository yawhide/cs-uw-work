/*
* Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
*	The President and Fellows of Harvard College.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
* 3. Neither the name of the University nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

/*
* Sample/test code for running a user program.  You can use this for
* reference when implementing the execv() system call. Remember though
* that execv() needs to do more than this function does.
*/

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
* Load program "progname" and start running it in usermode.
* Does not return except on error.
*
* Calls vfs_open on progname and thus may destroy it.
*/
int
runprogram(char *progname, char** argv, unsigned long argc)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int totalStringSize = 0;
	int totalPtrSize = 0;
	int err;

	for (unsigned long i = 0; i < argc; ++i){
		totalStringSize += (strlen(argv[i])+1);
	}
	totalStringSize = ROUNDUP(totalStringSize, 8);
	totalPtrSize = ROUNDUP((argc+1)*4, 8);
	//kprintf("sizes for stack: %d, %d\n", totalStringSize, totalPtrSize);
	//kprintf("progname: %s, argc:%d\n", progname, (int)argc);

/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);
/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}
/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();
/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}
/* Done with the file now. */
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

	char** longChar = kmalloc(argc * sizeof(char*));
	for (unsigned long i = 0; i < argc; ++i){
		longChar[i] = kmalloc((strlen(argv[i])+1) * sizeof(char));
		strcpy(longChar[i], argv[i]);
	}
	stackaddr = stackptr - totalStringSize;

	for (unsigned long i = 0; i < argc; ++i){
		size_t len;
		err = copyoutstr(longChar[i], (userptr_t)stackaddr, strlen(longChar[i])+1, &len);
		if(err) return err;
		stackaddr+= (strlen(longChar[i])+1);
	}
	stackaddr = stackptr - totalStringSize;

	for(unsigned long i = 0; i <= argc; i++){
		if(i == argc){
			stringAddrs[i] = (vaddr_t)NULL;
		} else {
			stringAddrs[i] = stackaddr + acc;
			acc = acc + strlen(argv[i]) +1;
		}
	}

	stackaddr = stackptr - totalStringSize - totalPtrSize;
	for (unsigned long i = 0; i <= argc; i++){
		err = copyout(&stringAddrs[i], (userptr_t)stackaddr, sizeof(vaddr_t));
		if(err) return err;
		stackaddr += 4;
	}

	stackaddr = stackptr - totalStringSize;
	stackptr = stackptr - totalStringSize - totalPtrSize;

//curproc_setas(NULL);

/* Warp to user mode. */
	//kprintf("argc: %d, stackaddr: %p, stackptr: %p\n", (int)argc, (void *)stackaddr, (void *)stackptr);

	enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);

/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

