#include "config.h"
#include "globals.h"

#include "errno.h"

#include "util/init.h"
#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

#include "mm/slab.h"
#include "mm/page.h"

kthread_t *curthr; /* global */
static slab_allocator_t *kthread_allocator = NULL;

#ifdef __MTP__
/* Stuff for the reaper daemon, which cleans up dead detached threads */
static proc_t *reapd = NULL;
static kthread_t *reapd_thr = NULL;
static ktqueue_t reapd_waitq;
static list_t kthread_reapd_deadlist; /* Threads to be cleaned */

static void *kthread_reapd_run(int arg1, void *arg2);
#endif

void
kthread_init()
{
        kthread_allocator = slab_allocator_create("kthread", sizeof(kthread_t));
        KASSERT(NULL != kthread_allocator);
}

/**
 * Allocates a new kernel stack.
 *
 * @return a newly allocated stack, or NULL if there is not enough
 * memory available
 */
static char *
alloc_stack(void)
{
        /* extra page for "magic" data */
        char *kstack;
        int npages = 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT);
        kstack = (char *)page_alloc_n(npages);

        return kstack;
}

/**
 * Frees a stack allocated with alloc_stack.
 *
 * @param stack the stack to free
 */
static void
free_stack(char *stack)
{
        page_free_n(stack, 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT));
}

/*
 * Allocate a new stack with the alloc_stack function. The size of the
 * stack is DEFAULT_STACK_SIZE.
 *
 * Don't forget to initialize the thread context with the
 * context_setup function. The context should have the same pagetable
 * pointer as the process.
 */
kthread_t *
kthread_create(struct proc *p, kthread_func_t func, long arg1, void *arg2)
{
/*
	-allocate the slab
	-allocate the stack
	-setup the owning process
	-setup the context for new thread
	-set the state
	-pass to the scheduler to put in run.
	-return the created thread.
*/
	    kthread_t *obj, *tail;
			 KASSERT(NULL != p);
		obj=(kthread_t*)slab_obj_alloc(kthread_allocator);

		obj->kt_kstack=alloc_stack();
		obj->kt_proc=p;
		/*Initialize remaining params to defaults.*/
		obj->kt_errno=0;
		obj->kt_cancelled=0;
		obj->kt_retval=0;
		obj->kt_wchan=NULL;
		obj->kt_state=KT_RUN;

		list_link_init(&obj->kt_plink);

/*
		obj->kt_plink.l_next=&obj->kt_plink;
		obj->kt_plink.l_prev=&obj->kt_plink;
*/

		list_link_init(&obj->kt_qlink);
/*
		obj->kt_qlink.l_next=&obj->kt_qlink;
		obj->kt_qlink.l_prev=&obj->kt_qlink;
*/

		list_insert_tail(&p->p_threads,&obj->kt_plink);

		dbg_print("kthread_create: thread data structures created\n");
		context_setup(&(obj->kt_ctx),func,(int)arg1,arg2,obj->kt_kstack,DEFAULT_STACK_SIZE,p->p_pagedir);
		dbg_print("kthread_create: Context setup successful\n");
		/*NOT_YET_IMPLEMENTED("PROCS: kthread_create");*/
/*		sched_make_runnable(obj);

		sched_switch();
*/		return obj;
}

void
kthread_destroy(kthread_t *t)
{
        KASSERT(t && t->kt_kstack);
        free_stack(t->kt_kstack);
        if (list_link_is_linked(&t->kt_plink))
                list_remove(&t->kt_plink);

        slab_obj_free(kthread_allocator, t);
}

/*
 * If the thread to be cancelled is the current thread, this is
 * equivalent to calling kthread_exit. Otherwise, the thread is
 * sleeping and we need to set the cancelled and retval fields of the
 * thread.
 *
 * If the thread's sleep is cancellable, cancelling the thread should
 * wake it up from sleep.
 *
 * If the thread's sleep is not cancellable, we do nothing else here.
 */
void
kthread_cancel(kthread_t *kthr, void *retval)
{
	dbg_print("kthread_cancel: Canceling thread\n");

	kthr->kt_retval=retval;
	kthr->kt_cancelled=1;
	KASSERT(NULL != kthr);
	if(kthr==curthr)
	{
		kthread_exit(retval);
		dbg_print("kthread_cancel: Canceled current thread\n");
	}
	else
	{

		if(kthr->kt_state==KT_SLEEP_CANCELLABLE)
		{
			sched_cancel(kthr);
		}
		else
			dbg_print("kthread_cancel: Could not cancel thread as its sleep is not cancelable\n");
	}
/*        NOT_YET_IMPLEMENTED("PROCS: kthread_cancel"); */
}

/*
 * You need to set the thread's retval field, set its state to
 * KT_EXITED, and alert the current process that a thread is exiting
 * via proc_thread_exited.
 *
 * It may seem unneccessary to push the work of cleaning up the thread
 * over to the process. However, if you implement MTP, a thread
 * exiting does not necessarily mean that the process needs to be
 * cleaned up.
 */
void
kthread_exit(void *retval)
{
	dbg_print("kthread_exit: Exiting thread\n");

	curthr->kt_retval=retval;
	curthr->kt_state=KT_EXITED;
	KASSERT(!curthr->kt_wchan);
	KASSERT(!curthr ->kt_qlink.l_next && !curthr->kt_qlink.l_prev);
	KASSERT(curthr->kt_proc == curproc);

/*
	Confirm if these 2 lines are required.
	list_remove(curthr->kt_plink);
	list_remove(&curthr->kt_qlink);
*/

	proc_thread_exited(retval);

	dbg_print("kthread_exit: Exited thread\n");

/*        NOT_YET_IMPLEMENTED("PROCS: kthread_exit"); */
}

/*
 * The new thread will need its own context and stack. Think carefully
 * about which fields should be copied and which fields should be
 * freshly initialized.
 *
 * You do not need to worry about this until VM.
 */
kthread_t *
kthread_clone(kthread_t *thr)
{
/*
        NOT_YET_IMPLEMENTED("VM: kthread_clone");
        return NULL;
*/
	kthread_t* forked_thr=(kthread_t*)slab_obj_alloc(kthread_allocator);

	forked_thr->kt_kstack = alloc_stack();
	/*doubt: forked_thr->kt_proc=how to get forked proc's reference here*/
	forked_thr->kt_errno = 0;
	forked_thr->kt_cancelled = 0;
	forked_thr->kt_retval = 0;
	/*wchan: will new thread wait on same q as its parent???*/
	/*ideally will not make a diff as
	 * parent thr will not be waiting coz if the thread has to be running to excute a fork instruc*/

	forked_thr->kt_wchan = thr->kt_wchan;
	/*kt_state probably need to copy. since other option is to set to run by default and in this case kassert is useless*/
	forked_thr->kt_state = thr->kt_state;

	list_link_init(&forked_thr->kt_plink);
	list_link_init(&forked_thr->kt_qlink);
	list_insert_tail(&forked_thr->kt_proc->p_threads,&forked_thr->kt_plink);

	dbg(DBG_PRINT|DBG_VM,"KTHREAD_CLONE: thread data structures initialized\n");
	KASSERT(KT_RUN == forked_thr->kt_state);
	dbg(DBG_PRINT|DBG_VM,"KASSERT(KT_RUN == forked_thr->kt_state) executed successfully\n");
	/*Will be reset in do_fork*/
/*
	context_setup(&(forked_thr->kt_ctx), func, 0, NULL, forked_thr->kt_kstack,
			DEFAULT_STACK_SIZE, curproc->p_pagedir);
*/
	return forked_thr;
}

/*
 * The following functions will be useful if you choose to implement
 * multiple kernel threads per process. This is strongly discouraged
 * unless your weenix is perfect.
 */
#ifdef __MTP__
int
kthread_detach(kthread_t *kthr)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_detach");
        return 0;
}

int
kthread_join(kthread_t *kthr, void **retval)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_join");
        return 0;
}

/* ------------------------------------------------------------------ */
/* -------------------------- REAPER DAEMON ------------------------- */
/* ------------------------------------------------------------------ */
static __attribute__((unused)) void
kthread_reapd_init()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_init");
}
init_func(kthread_reapd_init);
init_depends(sched_init);

void
kthread_reapd_shutdown()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_shutdown");
}

static void *
kthread_reapd_run(int arg1, void *arg2)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_run");
        return (void *) 0;
}
#endif




