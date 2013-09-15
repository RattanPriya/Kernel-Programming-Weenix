#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"
#include "mm/kmalloc.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/shadowd.h"

#define SHADOW_SINGLETON_THRESHOLD 5

int shadow_count = 0; /* for debugging/verification purposes */
#ifdef __SHADOWD__
/*
 * number of shadow objects with a single parent, that is another shadow
 * object in the shadow objects tree(singletons)
 */
static int shadow_singleton_count = 0;
#endif

static slab_allocator_t *shadow_allocator;

static void shadow_ref(mmobj_t *o);
static void shadow_put(mmobj_t *o);
static int shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite,
		pframe_t **pf);
static int shadow_fillpage(mmobj_t *o, pframe_t *pf);
static int shadow_dirtypage(mmobj_t *o, pframe_t *pf);
static int shadow_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t shadow_mmobj_ops = { .ref = shadow_ref, .put = shadow_put,
		.lookuppage = shadow_lookuppage, .fillpage = shadow_fillpage,
		.dirtypage = shadow_dirtypage, .cleanpage = shadow_cleanpage };

/*
 * This function is called at boot time to initialize the
 * shadow page sub system. Currently it only initializes the
 * shadow_allocator object.
 */
void shadow_init() {
	/*      NOT_YET_IMPLEMENTED("VM: shadow_init");*/

	shadow_allocator = slab_allocator_create("Shadow Allocator", sizeof(mmobj_t));
	KASSERT(shadow_allocator);
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_INIT: KASSERT(anon_allocator) executed successfully\n");

	dbg(DBG_PRINT|DBG_VM, "SHADOW_INIT: Returning successfully\n");
}

/*
 * You'll want to use the shadow_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
shadow_create() {
	/*
	 NOT_YET_IMPLEMENTED("VM: shadow_create");
	 return NULL;
	 */
	mmobj_t *mmobj = NULL;
	mmobj = (mmobj_t *) slab_obj_alloc(shadow_allocator);
	mmobj->mmo_nrespages=0;
	mmobj->mmo_refcount=0;
	list_init(&(mmobj)->mmo_respages);
	list_init(&(mmobj)->mmo_un.mmo_vmas);
	dbg(DBG_PRINT | DBG_VM, "SHADOW_CREATE: created mmobj\n");
	return mmobj;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void shadow_ref(mmobj_t *o) {
	KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_REF: KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops)) executed successfully\n");

	o->mmo_refcount++;
	dbg(DBG_PRINT | DBG_VM, "SHADOW_REF: RefCount incremented to %d\n",
			o->mmo_refcount);
	/*        NOT_YET_IMPLEMENTED("VM: shadow_ref");*/
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is a shadow object, it will never
 * be used again. You should unpin and uncache all of the object's
 * pages and then free the object itself.
 */
static void shadow_put(mmobj_t *o) {
	int respg_count;
	pframe_t *temp;
	KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops));
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_PUT: KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops)) executed successfully\n");

	if (o->mmo_refcount > 0)
		o->mmo_refcount--;
	respg_count = 0;

	list_iterate_begin(&o->mmo_respages, temp,
			pframe_t, pf_olink)
				{
					respg_count++;
				}list_iterate_end();

	if (o->mmo_refcount == respg_count) {
		dbg(DBG_PRINT | DBG_VM,
				"SHADOW_PUT: Refcount is now equal to Resident page count. Unpinning and Uncaching...\n");
		/*Object is no longer in use*/
		list_iterate_begin(&o->mmo_respages, temp,
				pframe_t, pf_olink)
					{
						pframe_unpin(temp);
						/*check some conditions before uncache*/
						pframe_free(temp);
					}list_iterate_end();
				}
				/*This free or some specific free*/
	kfree(o);
	dbg(DBG_PRINT | DBG_VM, "SHADOW_PUT: Returning successfully\n");
}

/* This function looks up the given page in this shadow object. The
 * forwrite argument is true if the page is being looked up for
 * writing, false if it is being looked up for reading.
 *
 * This function
 * must handle all do-not-copy-on-not-write magic (i.e. when forwrite
 * is false find the first shadow object in the chain which has the
 * given page resident).
 *
 * copy-on-write magic (necessary when forwrite
 * is true) is handled in shadow_fillpage, not here. */
static int shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite,
		pframe_t **pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: shadow_lookuppage");
	 return 0;
	 */
	mmobj_t *temp;
	int retval = -1;
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_LOOKUPPAGE: Entered \n");

	if (forwrite == 1) {
		retval = pframe_get(o, pagenum, pf);
	} else {

		while (retval < 0) {
			retval = o->mmo_ops->lookuppage(o, pagenum, forwrite,pf);
			if (retval == 0) {
				dbg(DBG_PRINT | DBG_VM,
						"SHADOW_LOOKUPPAGE: Returning with return value %d \n",
						retval);
				return 0;
			}
			/*DOUBT: Next line may not be correct: should iterate to next
			 * object in the "chain" as mentioned in comments*/
			/*if (o->mmo_un != NULL)*/
				o = o->mmo_shadowed;
			/*else {
				dbg(DBG_PRINT | DBG_VM,
						"SHADOW_LOOKUPPAGE: Returning with return value %d \n",
						-1);
				return -1;
			}*/
			if (NULL == o) {
				dbg(DBG_PRINT | DBG_VM,
						"SHADOW_LOOKUPPAGE: Returning with return value %d \n",
						-1);
				return -1;
			}
		}
	}
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_LOOKUPPAGE: Returning with return value %d \n", retval);
	return retval;
}

/* As per the specification in mmobj.h, fill the page frame starting
 * at address pf->pf_addr with the contents of the page identified by
 * pf->pf_obj and pf->pf_pagenum. This function handles all
 * copy-on-write magic (i.e. if there is a shadow object which has
 * data for the pf->pf_pagenum-th page then we should take that data,
 * if no such shadow object exists we need to follow the chain of
 * shadow objects all the way to the bottom object and take the data
 * for the pf->pf_pagenum-th page from the last object in the chain). */
static int shadow_fillpage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: shadow_fillpage");
	 return 0;
	 */
	int retval = -1;
	KASSERT(pframe_is_busy(pf));
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_FILLPAGE: KASSERT(pframe_is_busy(pf)) executed successfully\n");

	KASSERT(!pframe_is_pinned(pf));
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_FILLPAGE: KASSERT(!pframe_is_pinned(pf)) executed successfully\n");

	while (retval < 0) {
		pf->pf_obj = o;
		retval = o->mmo_ops->fillpage(o,pf);
		if (retval == 0) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_FILLPAGE: Returning with return value %d \n",
					retval);
			return 0;
		}
		o = o->mmo_shadowed;
		if (NULL == o) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_FILLPAGE: Returning with return value %d \n", -1);
			return -1;
		}
	}
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_FILLPAGE: Returning with return value %d \n", retval);
	return retval;

}

/* These next two functions are not difficult. */

static int shadow_dirtypage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: shadow_dirtypage");
	 return -1;
	 */
	int retval = -1;
	while (retval < 0) {
		pf->pf_obj = o;
		retval = o->mmo_ops->dirtypage(o,pf);
		if (retval == 0) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_DIRTYPAGE: Returning with return value %d \n",
					retval);
			return 0;
		}
			o = o->mmo_shadowed;
		if (NULL == o) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_DIRTYPAGE: Returning with return value %d \n", -1);
			return -1;
		}
	}
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_DIRTYPAGE: Returning with return value %d \n", retval);
	return retval;

}

static int shadow_cleanpage(mmobj_t *o, pframe_t *pf) {
	/*
	 NOT_YET_IMPLEMENTED("VM: shadow_cleanpage");
	 return -1;
	 */
	int retval = -1;
	while (retval < 0) {
		pf->pf_obj = o;

		retval = o->mmo_ops->cleanpage(o,pf);
		if (retval == 0) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_CLEANPAGE: Returning with return value %d \n",
					retval);
			return 0;
		}
			o = o->mmo_shadowed;
		if (NULL == o) {
			dbg(DBG_PRINT | DBG_VM,
					"SHADOW_CLEANPAGE: Returning with return value %d \n", -1);
			return -1;
		}
	}
	dbg(DBG_PRINT | DBG_VM,
			"SHADOW_CLEANPAGE: Returning with return value %d \n", retval);
	return retval;

}

