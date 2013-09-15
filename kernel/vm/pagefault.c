#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/proc.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/pagetable.h"

#include "vm/pagefault.h"
#include "vm/vmmap.h"

/*
 * This gets called by _pt_fault_handler in mm/pagetable.c The
 * calling function has already done a lot of error checking for
 * us. In particular it has checked that we are not page faulting
 * while in kernel mode. Make sure you understand why an
 * unexpected page fault in kernel mode is bad in Weenix. You
 * should probably read the _pt_fault_handler function to get a
 * sense of what it is doing.
 *
 * Before you can do anything you need to find the vmarea that
 * contains the address that was faulted on. Make sure to check
 * the permissions on the area to see if the process has
 * permission to do [cause]. If either of these checks does not
 * pass kill the offending process, setting its exit status to
 * EFAULT (normally we would send the SIGSEGV signal, however
 * Weenix does not support signals).
 *
 * Now it is time to find the correct page (don't forget
 * about shadow objects, especially copy-on-write magic!). Make
 * sure that if the user writes to the page it will be handled
 * correctly.
 *
 * Finally call pt_map to have the new mapping placed into the
 * appropriate page table.
 *
 * @param vaddr the address that was accessed to cause the fault
 *
 * @param cause this is the type of operation on the memory
 *              address which caused the fault, possible values
 *              can be found in pagefault.h
 */
void
handle_pagefault(uintptr_t vaddr, uint32_t cause)
{
/*        NOT_YET_IMPLEMENTED("VM: handle_pagefault");*/

	int retval,forwrite=0;
	pframe_t **pf=NULL;
	vmarea_t *vma;
	uint32_t pgnum,pdflags,ptflags;
	uintptr_t paddr;
/*
	 1) find the vmarea that contains the address that was faulted on.
	 If either of these checks does not pass
		a) kill the offending process,
		b) setting its exit status to EFAULT
*/

	pgnum = ADDR_TO_PN(curproc->p_brk);
	dbg(DBG_PRINT,
				"\n...calling vmmap lookup from handle_pagefault..\n");
	vma=vmmap_lookup(curproc->p_vmmap,pgnum);
	if(vma==NULL)
	{
		proc_kill(curproc,-EFAULT);
		return;
	}

/*
	 2) Make sure to check the permissions on the area to see if the process has
	 permission to do [cause].
	 If either of these checks does not pass
	 	a) kill the offending process,
	 	b) setting its exit status to EFAULT
*/
	if((vma->vma_prot&cause)!=cause)
	{
		proc_kill(curproc,-EFAULT);
		return;
	}
/*

	 3) Now it is time to find the correct page (don't forget
	 about shadow objects, especially copy-on-write magic!).
*/
/*
	 4)Make sure that if the user writes to the page it will be handled
	 correctly.
*/
	if(cause==FAULT_WRITE)
		forwrite=1;
	retval=vma->vma_obj->mmo_ops->lookuppage(vma->vma_obj,pgnum,forwrite,pf);
	if (retval < 0) {
		return;
	}

/*
	 5) Finally call pt_map to have the new mapping placed into the
	 appropriate page table.
*/
	paddr=(uintptr_t)PN_TO_ADDR(pgnum);
	retval=pt_map(curproc->p_pagedir,vaddr,paddr,PD_PRESENT,PT_WRITE);
/*
 * Maps the given physical page in at the given virtual page in the
 * given page directory. Creates a new page table if necessary and
 * places an entry in it in the page directory. vaddr must be in the
 * user address space.
 *
 * Both vaddr and paddr must be page aligned.
 * Note that the TLB is not flushed by this function.
 *
	Currently we are passing vaddr, pf->pf_addr, PD_PRESENT|PD_WRITE, PT_PRESENT| PT_WRITE
	 If you look at all the possible flags, there are not that many choices.  So, figure out whats
	 dependent on the cause.  For example, if a page is not suppose to be writable, you should not
	 set the *_WRITE flag.  And if a page is suppose to be writable, you should set the *_WRITE flag.
*/

	return;
}

