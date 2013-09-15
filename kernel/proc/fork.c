#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int do_fork(struct regs *regs) {
	/*
	 NOT_YET_IMPLEMENTED("VM: do_fork");
	 return 0;
	 */

	int retval = 0,fd;
	char *new_name=NULL;
	proc_t *forked_proc;
	kthread_t *forked_thr;
	strcat(new_name,curproc->p_comm);
	strcat(new_name,"-forked");

	dbg(DBG_PRINT | DBG_VM,
				"\n\ncalling from do_fork................ %d\n", retval);

	forked_proc=proc_create(new_name);
	if(forked_proc!=NULL)
	{
		/*clone vmmap from parent to the forked process*/
		forked_proc->p_vmmap=vmmap_clone(curproc->p_vmmap);

		/*IMP:TODO: Remember to increase
the reference counts on the underlying memory objects*/

/*
		TODO parts: For each private mapping in the original process, point the virtual memory
		areas of the new and old processes to two new shadow objects, which in
		turn should point to the original underlying memory object. This is how
		you know that pages corresponding to this mapping are copy-on-write. Be
		careful with reference counts. Also note that for shared mappings, there
		is no need to make a shadow object.
*/


/*
		TODO: Unmap the userland page table entries and ush the TLB using pt unmap range()
		and tlb flush all(). This is necessary because the parent process might
		still have some entries marked as \writable", but since we are implement-
		ing copy-on-write we would like access to these pages to cause a trap to
		our page fault handler so it can dirty the page, which will invoke the
		copy-on-write actions.
*/

		forked_thr=kthread_clone(curthr);
		forked_thr->kt_proc->p_pagedir=curproc->p_pagedir;
		forked_thr->kt_ctx.c_eip=curthr->kt_ctx.c_eip;
		forked_thr->kt_ctx.c_esp=fork_setup_stack(regs,curthr->kt_kstack);
		forked_thr->kt_ctx.c_kstacksz=DEFAULT_STACK_SIZE;

		/*copied file table*/
		for (fd = 0; fd < NFILES; fd++) {
			if(forked_thr->kt_proc->p_files[fd]!=NULL)
				forked_thr->kt_proc->p_files[fd]=curproc->p_files[fd];
				fref(forked_thr->kt_proc->p_files[fd]) ;
		}

		/*Set the child's working directory to point to the parent's working directory.*/
		forked_proc->p_cwd=curproc->p_cwd;
		vref(forked_proc->p_cwd);
		sched_make_runnable(forked_thr);
	}

	dbg(DBG_PRINT | DBG_VM,
			"DO_FORK: Returning with return value %d\n", retval);
	return retval;
}

