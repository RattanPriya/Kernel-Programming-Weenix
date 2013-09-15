#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void vmmap_init(void) {
	vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
	KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
	vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
	KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void) {
	vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
	if (newvma) {
		newvma->vma_vmmap = NULL;
	}
	return newvma;
}

void vmarea_free(vmarea_t *vma) {
	KASSERT(NULL != vma);
	slab_obj_free(vmarea_allocator, vma);
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */

vmmap_t *
vmmap_create(void) {
	KASSERT(NULL!=vmmap_allocator);
	vmmap_t *map = (vmmap_t*) slab_obj_alloc(vmmap_allocator);

	KASSERT(NULL != map && "Failed to allocate vmmap object!\n");
	dbg(DBG_PRINT|DBG_VM, "VMMAP_CREATE: KASSERT map!=NULL executed successfully\n");

	list_init(&map->vmm_list);
	dbg(DBG_PRINT|DBG_VM, "VMMAP_CREATE: Successfully initialized\n");
	return map;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void vmmap_destroy(vmmap_t *map) {
	vmarea_t *curr;
	int count=0;

	dbg(DBG_PRINT|DBG_VM, "VMMAP_DESTROY: Entered  by %s\n", curproc->p_comm);

	KASSERT( NULL!=map);
	dbg(DBG_PRINT|DBG_VM, "VMMAP_DESTROY: KASSERT map!=NULL executed successfully\n");

	/*find the number of VMAREAS in the map*/
	list_iterate_begin(&map->vmm_list,curr,vmarea_t,vma_plink){
		/*VMREMOVE from 0 to number of vmareas according to count*/
		vmmap_remove(map,curr->vma_start,curr->vma_end-curr->vma_start+1);
	}list_iterate_end();

	KASSERT(list_empty(&map->vmm_list) && "Failed to remove all vmareas from map!\n");
	/*Free the map since its not being used*/
	slab_obj_free(vmmap_allocator,map);
	dbg(DBG_PRINT|DBG_VM, "VMMAP_DESTROY: Successful! Returning.. \n");
	return;
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void vmmap_insert(vmmap_t *map, vmarea_t *newvma) {
	vmarea_t *curr, *prev;
	int found=0;
	KASSERT(NULL != map && NULL != newvma);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_INSERT: KASSERT(NULL != map && NULL != newvma) executed successfully\n");

	KASSERT(newvma->vma_start < newvma->vma_end);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_INSERT: KASSERT(newvma->vma_start < newvma->vma_end) executed successfully\n");

	KASSERT(NULL == newvma->vma_vmmap);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_INSERT: KASSERT(NULL == newvma->vma_vmmap) executed successfully\n");

	KASSERT(
			ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_INSERT: KASSERT(ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end) executed successfully\n");

/*	list_insert_tail(&map->vmm_list, &newvma->vma_plink);*/

/*
	CHECK LOGIC AGAIN!!
	1. iterate map->list;
	2. if newma->start > map->area->end , insert after map->area->end;
	3. if newma->end < map->area->start , insert before map->area->end;
*/
	newvma->vma_vmmap = map;

	list_iterate_begin(&map->vmm_list,curr,vmarea_t,vma_plink){
		/*handle 1st position insert*/
		dbg(DBG_PRINT|DBG_VM,
				"VMMAP_INSERT: Iteration started...............\n");
		if(prev==NULL && newvma->vma_end < curr->vma_start)
		{
			found=1;
			list_insert_head(&map->vmm_list, &newvma->vma_plink);
			dbg(DBG_PRINT|DBG_VM,
					"VMMAP_INSERT: Inserted successfully\n");

			return;
		}
		if(newvma->vma_start > prev->vma_end && newvma->vma_end < curr->vma_start)
		{
			found=1;
			list_insert_before(&curr->vma_plink, &newvma->vma_plink);
			dbg(DBG_PRINT|DBG_VM,
					"VMMAP_INSERT: Inserted successfully\n");
			return;
		}
		prev=curr;
	}list_iterate_end();
	/*Dunno if the next loop is reqd. Will above code handle insert after last position????*/
	if(found==0)
	{
		dbg(DBG_PRINT|DBG_VM,
				"VMMAP_INSERT: Inserted successfully\n");
		list_insert_tail(&map->vmm_list, &newvma->vma_plink);
		return;
	}

	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_INSERT: Returning unsuccessfully\n");
	return;
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int vmmap_find_range(vmmap_t *map, uint32_t npages, int dir) {

	vmarea_t *curr=NULL,*prev=NULL;
	uint32_t last_free_start_vfn = 0;
	uint32_t free_pgs = 0;
	int found=0;
	/*Precondition KASSERTS*/
	KASSERT(NULL!=map);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_FIND_RANGE: KASSERT(NULL!=map) executed successfully\n");
	KASSERT(0 < npages);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_FIND_RANGE: KASSERT(0 < npages); executed successfully\n");
	KASSERT(dir==VMMAP_DIR_HILO || dir ==VMMAP_DIR_LOHI); /*additional kassert to avoid an else if*/

	/*Boundary handling not reqd right? */
	list_iterate_begin(&map->vmm_list,curr,vmarea_t,vma_plink){
		if(prev!=NULL)
		{
			free_pgs=(curr->vma_start-prev->vma_end-1);
			if(free_pgs>=npages){
				found=1;
				if(dir==VMMAP_DIR_LOHI)
				{	dbg(DBG_PRINT|DBG_VM,
						"VMMAP_FIND_RANGE: Located space. Returning successfully\n");
					return (prev->vma_end+1);
				}
				else
					last_free_start_vfn=curr->vma_start-npages;
			}
		}
		prev=curr;
	}list_iterate_end();

	if (found==1)
	{
		dbg(DBG_PRINT|DBG_VM,
			"VMMAP_FIND_RANGE: Located space. Returning successfully\n");
		return last_free_start_vfn;
	}

	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_FIND_RANGE: No space. Returning\n");
	return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn) {
	vmarea_t *vm;
	/*Precondition KASSERTS*/
	KASSERT(NULL!=map);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_LOOKUP: KASSERT(NULL!=map) executed successfully\n");

	list_iterate_begin(&map->vmm_list,vm,vmarea_t,vma_plink){
		dbg(DBG_PRINT,
							"VMMAP_LOOKUP: vfn %d ... start %d ... end %d\n", vfn,vm->vma_start,vm->vma_end);
		if (vfn >= vm->vma_start && vfn <= vm->vma_end) {
			dbg(DBG_PRINT,
					"VMMAP_LOOKUP: The given vfn %d belongs to this map\n", vfn);
			return vm;
		}
		/*if the first area did not contain the vfn, then it will return NULL immediately and not iterate the rest of the list!*/
		/*else {
			return NULL;
		}*/
	}list_iterate_end();
	dbg(DBG_PRINT,
			"VMMAP_LOOKUP: The given vfn %d does not belong to this map\n", vfn);

	return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map) {

	vmmap_t *new_map = NULL;
	vmarea_t *vma, *new_vma;
	/*allocate a new map*/
	new_map = vmmap_create();

	/*add an area to the map*/
	list_iterate_begin(&map->vmm_list,vma,vmarea_t,vma_plink)
	{
		new_vma = vmarea_alloc();
		if (new_vma == NULL) {
			return NULL;
		} else {
			new_vma->vma_end=vma->vma_end;
			new_vma->vma_flags=vma->vma_flags; /*DOUBT: Copy flags?*/
			/*mmobj NOT set as mentioned in comments above*/
			new_vma->vma_off=vma->vma_off;
			/*DOUBT: What to do with olink??*/
			new_vma->vma_prot=vma->vma_prot;
			new_vma->vma_start=vma->vma_start;
			new_vma->vma_vmmap = new_map;
			list_insert_tail(&(new_map->vmm_list),&(new_vma->vma_plink));
		}
	}list_iterate_end();
	if(NULL!=new_map){
		dbg(DBG_PRINT|DBG_VM,
				"VMMAP_CLONE: Returning successfully\n");
		return new_map;
	}
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_CLONE: Returning unsuccessfully\n");
	return NULL;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.
 *
 *
 * Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
		int prot, int flags, off_t off, int dir, vmarea_t **new) {
	int start_vfn;
	mmobj_t *temp;
	vmarea_t *vma;
	int retval;
	KASSERT(off%PAGE_SIZE==0);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT(off is page aligned) executed successfully\n");

	KASSERT(NULL != map);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT(NULL != map) executed successfully\n");

	KASSERT(0 < npages);
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT(0 < npages) executed successfully\n");

	KASSERT(!(~(PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC) & prot));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT(!(~(PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC) & prot)) executed successfully\n");

	KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags)) executed successfully\n");

	KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage)) executed successfully\n");

	KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages)));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages))) executed successfully\n");

	KASSERT(PAGE_ALIGNED(off));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: KASSERT(PAGE_ALIGNED(off)) executed successfully\n");

	if(lopage==0)
	{
		start_vfn = vmmap_find_range(map, npages, dir);
		if(start_vfn<0){
			dbg(DBG_PRINT|DBG_VM,
					"VMMAP_MAP: lopage was 0 and find_range returned -1. Returning unsuccessfully\n");
			return start_vfn;
		}
	}
	else
	{
		dbg(DBG_PRINT,
						"\n...calling vmmap lookup from vmmap_map..\n");

		vma=vmmap_lookup(map,lopage);
		if(vma!=NULL)
		{
			retval=vmmap_remove(map,lopage,npages);
			if (retval<0)
				return retval;
		}

		start_vfn=lopage;
	}

	vma=vmarea_alloc();
	vma->vma_start=start_vfn;
	vma->vma_end=start_vfn+npages;
	vma->vma_off=off;
	vma->vma_vmmap=map;
	vma->vma_flags=flags;
	vma->vma_prot=prot;
	if(file==NULL)
	{
		temp=anon_create();
		if (temp==NULL)
			return -1;
	}
	else
	{
		KASSERT(file->vn_ops->mmap!=NULL);
		file->vn_ops->mmap(file,vma,&temp);
	}
	vma->vma_obj=temp;

	if(prot==MAP_PRIVATE){
		temp=shadow_create();
		temp->mmo_shadowed=vma->vma_obj;
		KASSERT(NULL!=vma->vma_obj->mmo_ops->ref);
		vma->vma_obj->mmo_ops->ref(vma->vma_obj);
	}
	if(new!=NULL)
		new=&vma;

	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_MAP: Returning successfully\n");
	return 0;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages) {

	vmarea_t *vma, *new_vma;

	if (vmmap_is_range_empty(map,lopage,npages))
	{
		return 0;
	}


	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink){
		if(vma->vma_start==lopage)
		{
			if(npages==vma->vma_end-vma->vma_start+1){
				/*CASE: [            ]
				 *       ************
				 */
				KASSERT(vma->vma_obj!=NULL);
				vma->vma_obj->mmo_ops->put(vma->vma_obj);
				list_remove(&(vma->vma_plink));
				return 0;
			}
			else if(npages>vma->vma_end-vma->vma_start+1){
				/*CASE: [            ]
				 *       ******************
				 */
				lopage=vma->vma_end+1;
				npages=npages-vma->vma_end-vma->vma_start+1;
				KASSERT(vma->vma_obj!=NULL);
				vma->vma_obj->mmo_ops->put(vma->vma_obj);
				list_remove(&(vma->vma_plink));
				/*CASE: [            ]
				 *       <removed>    ******
				 * Recursive call for remaining part.
				 */

				return vmmap_remove(map,lopage,npages);
			}
			else{/*if(npages<vma->vma_end-vma->vma_start+1)*/
				/*CASE: [            ]
				 *       *****
				 */
				vma->vma_start=vma->vma_start+npages;
				vma->vma_off=vma->vma_off+npages;
				return 0;
			}
		}
		else if(vma->vma_start<lopage)
		{
			/*			Check if lopage lies somewhere in the vmarea*/
			if(vma->vma_end>=lopage)
			{
				if(vma->vma_end-lopage+1==npages){
				/*CASE: [            ]
				 *            *******
				 */
					vma->vma_end=lopage-1;

					return 0;
				}
				else if((vma->vma_end-lopage+1)<=npages){
					/*CASE: [            ]
					 *            ************
					 */
					lopage=vma->vma_end+1;
					vma->vma_end=lopage-1;
					npages=npages-(vma->vma_end-lopage+1);
					return vmmap_remove(map,lopage,npages);
				}
				else{
					/*CASE: [            ]
					 *            *****
					 * Split into 2 vmareas
					 */
					new_vma=vmarea_alloc();
					*new_vma=*vma;
					new_vma->vma_end=lopage-1;
					vma->vma_start=lopage+npages-1;
					new_vma->vma_off=vma->vma_off;
					vma->vma_off=lopage+npages;
					list_insert_before(&vma->vma_plink,&new_vma->vma_plink);
					return 0;
				}
			}
/*
			else
			{
				CASE: [            ]
				                        ******************
				/Go to next
				 * iterate
				 * No need to write anything in this loop. will iterate on its own.
			}
*/
		}
		else
		{
			dbg(DBG_PRINT|DBG_VM,
					"VMMAP_REMOVE: Should not reach here. Check why vmmap_is_range_empty does not return that the page is not found.\n");
			return -1;
		}
	}list_iterate_end();
	return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages) {

	vmarea_t *vma;
	uint32_t endvfn=startvfn+npages-1;
	KASSERT((startvfn < startvfn+npages) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= startvfn+npages));
	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_IS_RANGE_EMPTY: KASSERT((startvfn < startvfn+npages) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= startvfn+npages)) executed successfully.\n");

	if(list_empty(&map->vmm_list)){

		dbg(DBG_PRINT|DBG_VM,
				"VMMAP_IS_RANGE_EMPTY: Range found to be empty since list is empty. Returning.\n");
	return 1;
	}
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
	{
		if ((startvfn >= vma->vma_start && startvfn <= vma->vma_end) ||
			(endvfn >= vma->vma_start && endvfn <= vma->vma_end)) {
			dbg(DBG_PRINT|DBG_VM,
					"VMMAP_IS_RANGE_EMPTY: Range found to be not empty. Returning.\n");
			return 0;
		}
	}list_iterate_end();

	dbg(DBG_PRINT|DBG_VM,
			"VMMAP_IS_RANGE_EMPTY: Range found to be empty. Returning.\n");
	return 1;

}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count) {
	uint32_t vfn;
	int retval;
	vmarea_t *vma;
	pframe_t **pf=NULL;
	size_t rcount;
	int fd;
	vfn = ADDR_TO_PN(vaddr);
	dbg(DBG_PRINT,
					"\n...calling vmmap lookup from vmmap_read..\n");

	vma = vmmap_lookup(map, vfn);
	if(vma==NULL)
		handle_pagefault(vaddr,PROT_READ);
	retval = pframe_get(vma->vma_obj, vfn, pf);
	if(retval<0)
		return retval;

	/*do something with vfn vma pf to get fd*/
	memcpy(buf,(*pf)->pf_addr,count);
	return 1;
}

/* Write from 'buf' into the virtual address space of 'map' starting at
* 'vaddr' for size 'count'. To do this, you will need to find the correct
* vmareas to write into, then find the correct pframes within those vmareas,
* and finally write into the physical addresses that those pframes correspond
* to. You should not check permissions of the areas you use. Assume (KASSERT)
* that all the areas you are accessing exist. Remember to dirty pages!
* Returns 0 on success, -errno on error.
*/

int vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count) {
	uint32_t vfn;
	int retval;
	vmarea_t *vma;
	pframe_t **pf=NULL;
	size_t wcount;
	int fd;
	vfn = ADDR_TO_PN(vaddr);
	dbg(DBG_PRINT,
					"\n...calling vmmap lookup from vmmap_write..\n");

	vma = vmmap_lookup(map, vfn);
	if(vma==NULL)
		handle_pagefault(vaddr,PROT_READ);

	retval = pframe_get(vma->vma_obj, vfn, pf);
	if(retval<0)
		return retval;

	memcpy((*pf)->pf_addr,buf,count);
	return 1;
}
/* a debugging routine: dumps the mappings of the given address space. */
size_t vmmap_mapping_info(const void *vmmap, char *buf, size_t osize) {
	KASSERT(0 < osize);
	KASSERT(NULL != buf);
	KASSERT(NULL != vmmap);

	vmmap_t *map = (vmmap_t *) vmmap;
	vmarea_t *vma;
	ssize_t size = (ssize_t) osize;

	int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n", "VADDR RANGE",
			"PROT", "FLAGS", "MMOBJ", "OFFSET", "VFN RANGE");

	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
				{
					size -= len;
					buf += len;
					if (0 >= size) {
						goto end;
					}

					len =
							snprintf(buf, size,
									"%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
									vma->vma_start << PAGE_SHIFT,
									vma->vma_end << PAGE_SHIFT,
									(vma->vma_prot & PROT_READ ? 'r' : '-'),
									(vma->vma_prot & PROT_WRITE ? 'w' : '-'),
									(vma->vma_prot & PROT_EXEC ? 'x' : '-'),
									(vma->vma_flags & MAP_SHARED ?
											" SHARED" : "PRIVATE"),
									vma->vma_obj, vma->vma_off, vma->vma_start,
									vma->vma_end);
				}list_iterate_end();

	end: if (size <= 0) {
		size = osize;
		buf[osize - 1] = '\0';
	}
	/*
	 KASSERT(0 <= size);
	 if (0 == size) {
	 size++;
	 buf--;
	 buf[0] = '\0';
	 }
	 */
	return osize - size;
}

