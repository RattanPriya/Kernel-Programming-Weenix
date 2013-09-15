#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/kmalloc.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"

int anon_count = 0; /* for debugging/verification purposes */

static slab_allocator_t *anon_allocator;

static void anon_ref(mmobj_t *o);
static void anon_put(mmobj_t *o);
static int anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite,
		pframe_t **pf);
static int anon_fillpage(mmobj_t *o, pframe_t *pf);
static int anon_dirtypage(mmobj_t *o, pframe_t *pf);
static int anon_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t anon_mmobj_ops = { .ref = anon_ref, .put = anon_put,
		.lookuppage = anon_lookuppage, .fillpage = anon_fillpage, .dirtypage =
				anon_dirtypage, .cleanpage = anon_cleanpage };

/*
 * This function is called at boot time to initialize the
 * anonymous page sub system. Currently it only initializes the
 * anon_allocator object.
 */
void anon_init() {
	/*      NOT_YET_IMPLEMENTED("VM: anon_init");*/
	anon_allocator = slab_allocator_create("AnonAllocator", sizeof(mmobj_t));
	KASSERT(anon_allocator);
	dbg(DBG_PRINT | DBG_VM,
			"ANON_INIT: KASSERT(anon_allocator) executed successfully\n");

	dbg(DBG_PRINT|DBG_VM, "ANON_INIT: Returning successfully\n");
}

/*
 * You'll want to use the anon_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
anon_create() {
	/*
	 NOT_YET_IMPLEMENTED("VM: anon_create");
	 return NULL;
	 */
	mmobj_t *mmobj = NULL;
	mmobj = (mmobj_t *) slab_obj_alloc(anon_allocator);
	mmobj->mmo_nrespages=0;
	mmobj->mmo_refcount=1;
	list_init(&(mmobj)->mmo_respages);
	list_init(&(mmobj)->mmo_un.mmo_vmas);
	/*Indiv member initialization*/

	dbg(DBG_PRINT | DBG_VM, "ANON_CREATE: created mmobj\n");
	return mmobj;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void anon_ref(mmobj_t *o) {
	/*	NOT_YET_IMPLEMENTED("VM: anon_ref");*/
	KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT | DBG_VM,
			"ANON_REF: KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops)) executed successfully\n");

	o->mmo_refcount++;
	dbg(DBG_PRINT | DBG_VM, "ANON_REF: RefCount incremented to %d\n",
			o->mmo_refcount);
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is an anonymous object, it will
 * never be used again. You should unpin and uncache all of the
 * object's pages and then free the object itself.
 */
static void anon_put(mmobj_t *o) {
	/*        NOT_YET_IMPLEMENTED("VM: anon_put");*/

	int respg_count;
	pframe_t *temp;
	KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT | DBG_VM,
			"ANON_PUT: KASSERT(o && (0 < o->mmo_refcount) && (&anon_mmobj_ops == o->mmo_ops)) executed successfully\n");

	if (o->mmo_refcount > 0)
		o->mmo_refcount--;
	respg_count = 0;

	list_iterate_begin(&o->mmo_respages, temp,pframe_t, pf_olink)
	{
		respg_count++;
	}list_iterate_end();

	if (o->mmo_refcount == respg_count) {
		dbg(DBG_PRINT | DBG_VM,
				"ANON_PUT: Refcount is now equal to Resident page count. Unpinning and Uncaching...\n");
		/*Object is no longer in use*/
		list_iterate_begin(&o->mmo_respages, temp,
				pframe_t, pf_olink)
					{
						pframe_unpin(temp);
/*						check some conditions before uncache*/
						pframe_free(temp);
					}list_iterate_end();
				}
	/*			This free or some specific free*/
	kfree(o);
	dbg(DBG_PRINT | DBG_VM, "ANON_PUT: Returning successfully\n");


/*	return o->mmo_ops->put(o);*/
}

/* Get the corresponding page from the mmobj. No special handling is
 * required. */
static int anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite,
		pframe_t **pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: anon_lookuppage");
	 return -1;
	 */

	/* Finds the correct page frame from a high-level perspective
	 * for performing the given operation on an area backed by
	 * the given pagenum of the given object. If "forwrite" is
	 * specified then the pframe should be suitable for writing;
	 * otherwise, it is permitted not to support writes. In
	 * either case, it must correctly support reads.
	 *
	 * Most objects will simply return a page from their
	 * own list of pages, but objects such as shadow objects
	 * may need to perform more complicated operations to find
	 * the appropriate page.
	 * This may block.
	 * Return 0 on success and -errno otherwise. */

	return o->mmo_ops->lookuppage(o, pagenum, forwrite, pf);
/*
 * commented to change to default.
	int retval = 0;
	retval = pframe_get(o, pagenum, pf);
	if (forwrite == 1 && pf->pf_flags == PF_DIRTY)
		return 0;
	dbg(DBG_PRINT | DBG_VM, "ANON_LOOKUPPAGE: Returning with return value = %d\n",retval);
	return retval;
*/
	/*
	 pframe_t *temp;
	 list_iterate_begin(o->mmo_respages, temp,
	 pframe_t, pf_olink){

	 if(temp->pf_pagenum==pagenum)
	 {
	 *pf=temp;
	 return -EINVAL;
	 }

	 } list_iterate_end();

	 return -1;
	 */
}

/* The following three functions should not be difficult. */

static int anon_fillpage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: anon_fillpage");
	 return 0;
	 */

	/* Fill the page frame starting at address pf->pf_paddr with the
	 * contents of the page identified by pf->pf_obj and pf->pf_pagenum.
	 * This may block.
	 * Return 0 on success and -errno otherwise.
	 */

	int retval = 0;
    KASSERT(pframe_is_busy(pf));
	dbg(DBG_PRINT | DBG_VM,
			"ANON_FILLPAGE: KASSERT(pframe_is_busy(pf)) executed successfully\n");

    KASSERT(!pframe_is_pinned(pf));
	dbg(DBG_PRINT | DBG_VM,
			"ANON_FILLPAGE: KASSERT(!pframe_is_pinned(pf)) executed successfully\n");

/*
	pf->pf_obj = o;
	retval = pframe_fill(pf);
*/

	dbg(DBG_PRINT | DBG_VM, "ANON_FILLPAGE: Returning with return value = %d\n",retval);

	/*return retval;*/
	return o->mmo_ops->fillpage(o, pf);

}

static int anon_dirtypage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: anon_dirtypage");
	 return -1;
	 */

	/*
	 * PFRAME BASIC FUNC DOES THIS:
	 * Indicates that a page is about to be modified. This should be called on a
	 * page before any attempt to modify its contents. This marks the page dirty
	 * (so that pageoutd knows to clean it before reclaiming the page frame)
	 * and calls the dirtypage mmobj entry point.
	 * The given page must not be busy.
	 *
	 * This routine can block at the mmobj operation level.
	 *
	 * @param pf the page to dirty
	 * @return 0 on success, -errno on failure
	 */
/*
	int retval = 0;
	pf->pf_obj = o;
	retval = pframe_dirty(pf);
	dbg(DBG_PRINT | DBG_VM, "ANON_DIRTYPAGE: Returning with return value = %d\n",retval);
	return retval;*/
	return o->mmo_ops->dirtypage(o, pf);
}

static int anon_cleanpage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: anon_cleanpage");
	 return -1;
	 */

	/* PFRAME BASE FUNC DOES THIS:
	 * Clean a dirty page by writing it back to disk. Removes the dirty
	 * bit of the page and updates the MMU entry.
	 * The page must be dirty but unpinned.
	 *
	 * This routine can block at the mmobj operation level.
	 * @param pf the page to clean
	 * @return 0 on success, -errno on failure
	 */
/*
	int retval = 0;
	pf->pf_obj = o;
	retval = pframe_clean(pf);
	dbg(DBG_PRINT | DBG_VM, "ANON_CLEANPAGE: Returning with return value = %d\n",retval);
	return retval;*/
	return o->mmo_ops->cleanpage(o,pf);
}

