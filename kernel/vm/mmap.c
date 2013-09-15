#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{

    /*
     * EPERM,
     *
     * */
int mask;
int error;
uint64_t error_overflow =0;
vmarea_t *vma = NULL;
    /*
     * I have not derived this code from http://kernelhistory.sourcentral.org/linux-1.1.63/S/380.html#L38
     *
     *BEGINS
     * */
/*HANDLE*/

file_t *myfile;
    if (fd < 0 || fd >= NFILES/*|| myfile->f_mode!=FMODE_READ*/) {
            dbg(DBG_ERROR,
                    "\nDO_READ: Error : Invalid fd / File cannot be read since fget failed. FD: %d", fd);
            return -EBADF;
        }
      /*  NOT_YET_IMPLEMENTED("VM: do_mmap");*/

    /*overflow of total mmarea of a process*--EAGAIN*/

error_overflow = len + off;
    if(error_overflow >  2147483647)
    {
        return -EOVERFLOW;
    }


    /*offset overflow**/

    if((int)off+(int)len < off)
    {
        return -EINVAL;

    }
    len = PAGE_ALIGNED(len);
    if(len==0)
    {
        return -EAGAIN;
    }
    /*BAD FILE DESCRIPTOR*/


    if(myfile!=NULL)
    {
        switch (flags & MAP_TYPE)
        {
        case MAP_SHARED:
            if((prot & PROT_WRITE) && !(myfile->f_mode & 2))
                    {
                        return -EACCES;
                    }
            /*if(lockf(myfile->f_vnode))
            {
                return -EAGAIN;
            }*/
            break;
        case MAP_PRIVATE:
        if(!(myfile->f_mode & 1))
            {
                return -EACCES;
            }
        break;
        default: return -EINVAL;
        }


    }
    else if((flags & MAP_TYPE) == MAP_SHARED)
    {
        return -EINVAL;
    }
    /*obtain address*/

    if(flags & MAP_FIXED)
    {
        if((int)addr & -PAGE_MASK)
        {
            return -EINVAL;
        }
        /*if(len > TASK_SIZE || addr > TASK_SIZE -len)
        {
            return -EINVAL;
        }*/
    }
    else  {

        page_free_n(addr,len);
        if(!addr)
        {
            return -ENOMEM;
        }

    }

    /*determine the object being mapped and call the appropriate specific mapper, the address has already been validated , but not unmapped , but the maps are removed from the lsit*/
    /*doubt: is this the condition reqd.*/
    if((myfile && (!(myfile->f_vnode->vn_ops))) || myfile->f_vnode->vn_ops->mmap)
    {
        return -ENODEV;
    }

    /*mask = PAGE_PRESENT;*/
    vma = vmarea_alloc();
    if(!vma)
    {
        return -ENOMEM;
    }
    vma->vma_vmmap->vmm_proc = curproc->p_vmmap;
    vma->vma_start =addr;
    vma->vma_end = addr+len;
    vma->vma_flags = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);
    /*vma->vma_flags|= & VM_GROWSDOWN | VM_DENYWRITE|VM_EXECUTABLE*/
   /* vma->vma_flags |= curproc->p_*/
    vma->vma_obj=NULL;
    vma->vma_off = off;
    vma->vma_vmmap->vmm_proc->p_files[fd]->f_vnode = NULL;

    /**SET SOME OTHER FLAGS TOO**/
    do_munmap(addr,len);/*Clear old maps*/

    if(myfile)
    {
        int error;
        error = myfile->f_vnode->vn_ops->mmap(myfile->f_vnode,myfile,vma);
        if(error)
        {
            kfree(vma);
            return error;

        }
    }

    flags= vma->vma_flags;

    list_insert_tail(&curproc->p_vmmap->vmm_list,&vma);
    /******************************/
    /*merge the two segments//*/

    /*merge_segments(curproc,vma->vm_start,vma->vm_end);*/

    /*merge_Segments might have our vma, so we cant use it any more*/

    /*-------------some more code----later!!*/

    KASSERT(NULL != curproc->p_pagedir);
    return 0;
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
        struct vmarea_t *mpnt,*prev,*next,**npp,*free;
        int mem_ok=0;
            if (((int)addr & ~PAGE_MASK)/* || addr > TASK_SIZE || len > TASK_SIZE-addr*/)
                    return -EINVAL;

            if(len==0)
            {
                return -EINVAL;
            }

           /* if ((len = PAGE_ALIGN(len)) == 0)
            {
                return -EINVAL;
            }
*/
            if (((int)addr)/PAGE_SIZE != 0)
            {
                 return -EINVAL;
            }

            vmarea_t *vma_itern;
            list_iterate_begin(&curproc->p_vmmap->vmm_list,vma_itern,vmarea_t,vma_plink)
            vmmap_remove(curproc->p_vmmap,&addr,(uint32_t)len/4096);
            list_iterate_end();
            /*How to clear the TLB???*/
            tlb_flush(addr);/*Doubt is this what is needed to be done?*/

            return 0;

}
