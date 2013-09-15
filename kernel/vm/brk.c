#include "globals.h"
#include "errno.h"
#include "util/debug.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/mman.h"
#include "mm/pframe.h"

#include "vm/mmap.h"
#include "vm/vmmap.h"
#include "proc/proc.h"

/*
 * This function implements the brk(2) system call.
 *
 * This routine manages the calling process's "break" -- the ending address
 * of the process's "dynamic" region (often also referred to as the "heap").
 * The current value of a process's break is maintained in the 'p_brk' member
 * of the proc_t structure that represents the process in question.
 *
 * The 'p_brk' and 'p_start_brk' members of a proc_t struct are initialized
 * by the loader. 'p_start_brk' is subsequently never modified; it always
 * holds the initial value of the break. Note that the starting break is
 * not necessarily page aligned!
 *
 * 'p_start_brk' is the lower limit of 'p_brk' (that is, setting the break
 * to any value less than 'p_start_brk' should be disallowed).
 *
 * The upper limit of 'p_brk' is defined by the minimum of (1) the
 * starting address of the next occuring mapping or (2) USER_MEM_HIGH.
 * That is, growth of the process break is limited only in that it cannot
 * overlap with/expand into an existing mapping or beyond the region of
 * the address space allocated for use by userland. (note the presence of
 * the 'vmmap_is_range_empty' function).
 *
 * The dynamic region should always be represented by at most ONE vmarea.
 * Note that vmareas only have page granularity, you will need to take this
 * into account when deciding how to set the mappings if p_brk or p_start_brk
 * is not page aligned.
 *
 * You are guaranteed that the process data/bss region is non-empty.
 * That is, if the starting brk is not page-aligned, its page has
 * read/write permissions.
 *
 * If addr is NULL, you should NOT fail as the man page says. Instead,
 * "return" the current break. We use this to implement sbrk(0) without writing
+3 * a separate syscall. Look in user/libc/syscall.c if you're curious.
 *
 * Also, despite the statement on the manpage, you MUST support combined use
 * of brk and mmap in the same process.
 *
 * Note that this function "returns" the new break through the "ret" argument.
 * Return 0 on success, -errno on failure.
 */
int
do_brk(void *addr, void **ret)
{
/*
        NOT_YET_IMPLEMENTED("VM: do_brk");
        return 0;
*/
	void *upper_lim;
	void* x;
	pframe_t **pf=NULL;
	vmarea_t *vma, *next,*temp;
	int now_set=0;
	uint32_t pgnum, next_mapping_start_pg;
	int retval;
	void *curr_page_start_addr;
	/*handle sbrk*/
	if (addr==NULL){
		*ret=curproc->p_brk;
		dbg(DBG_PRINT | DBG_VM,
				"DO_BRK: sbrk functionality handler returned curr p_brk. \n");
		return 0;
	}

/*
	Normal case
	 * The upper limit of 'p_brk' is defined by the minimum of (1) the
	 * starting address of the next occuring mapping or (2) USER_MEM_HIGH.
	 *
	 * The dynamic region should always be represented by at most ONE vmarea.
	 * Note that vmareas only have page granularity, you will need to take this
	 * into account when deciding how to set the mappings if p_brk or p_start_brk
	 * is not page aligned.
*/

	/*Check if new address crosses over to next mapping or max memory*/
	/*max is checked by USER_MAX_MEM*/
	/*Checking cross over :-
	 *  --> find vmarea of current page.
	 *  --> find the next vmarea after that
	 *  --> find the address of that vmareas start pg.
	 *  --> check if addr greater than that.
	 *  --> if yes return error
	 */
	/*DOUBT!!!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
	/*Support use of break as well as memory map--> how and where to handle this???*/
	/*DOUBT!!!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* --> FIND VMAREA OF CURRENT PAGE.. */
	/*p_brk and p_start break are not page aligned. handle this but how?:
	 This is handled i guess. Check if extra checks required.*/

	pgnum= ADDR_TO_PN(curproc->p_brk);
	vma=vmmap_lookup(curproc->p_vmmap,pgnum);
	if(vma==NULL)
	{
		dbg(DBG_PRINT | DBG_VM,
				"DO_BRK: Cannot find vmarea of the current p_brk. \n");
		return -1;
	}

	/*FIND NEXT VMAREA*/
	list_iterate_begin(&curproc->p_vmmap->vmm_list,temp,vmarea_t,vma_plink){
		if(temp==vma)
		{
			now_set=1;
		}
		if(now_set==1)
		{
			next = temp;
			now_set=0;
			break;
		}
	}list_iterate_end();

/*	next=&vma->vma_plink;*/

	/*FIND ADDR OF NEXT VMAREA's START PAGE*/
	next_mapping_start_pg=next->vma_start;
	retval = pframe_get(vma->vma_obj, next_mapping_start_pg, pf);
	if (retval<0)
		return retval;
	x=(*pf)->pf_addr;
	/*NOW x CONTAINS NEXT VMAREA's START ADDR*/

	if(USER_MEM_HIGH<x)
		upper_lim=(void *)USER_MEM_HIGH;
	else
		upper_lim=x;

	if(upper_lim>addr)
	{
		curproc->p_brk=addr;
		return 0;
	}
	else
		return -ENOMEM;
}

