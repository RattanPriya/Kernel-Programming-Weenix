#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
	dbg_print("proc_create: Allocating proc data-structures\n");
	/*create process and allocate a slab*/
	int fd;
	proc_t *proce = (proc_t*)slab_obj_alloc(proc_allocator);
	KASSERT(proce!=NULL);
	/*Get the next available ID for the process*/
	strcpy(proce->p_comm,name);
	proce->p_pproc= curproc;
	proce->p_state = PROC_RUNNING;
	proce->p_pid = _proc_getid();
	if(proce->p_pid==PID_INIT)
	{
		proc_initproc=proce;
	}

	/*Create Pagedir for the process*/
	proce->p_pagedir = pt_create_pagedir();
	/*check if its init process*/
	KASSERT(proce->p_pid!=PID_IDLE || list_empty(&_proc_list));

	KASSERT(PID_INIT!= proce->p_pid || PID_IDLE == curproc ->p_pid);
	proce->p_status = 0;

	list_init(&proce->p_threads);
	list_link_init(&proce->p_list_link);
	list_link_init(&proce->p_child_link);
	list_init(&(proce->p_children));
	list_insert_head(&(_proc_list),&(proce->p_list_link));

	if (proce->p_pid != PID_IDLE && curproc != NULL) {
		KASSERT(curproc!=NULL);
		list_insert_tail(&(curproc->p_children), &(proce->p_child_link));
	}

	sched_queue_init(&(proce->p_wait));

	/*for VFS*/
	proce->p_cwd = vfs_root_vn;

	/*KASSERT(NULL!=proce->p_cwd);*/

	if (proce->p_cwd) {
		vref(proce->p_cwd);
		KASSERT(proce->p_cwd!=NULL);
	}

	for (fd = 0; fd < NFILES; fd++) {
		proce->p_files[fd] = NULL;
	}

	proce->p_brk = NULL;
	proce->p_start_brk = NULL;
	proce->p_vmmap = vmmap_create();
	proce->p_vmmap->vmm_proc = proce;	/*proce->p_files[NFILES] = NULL;*/
	
	KASSERT(list_empty(&_proc_list) != 1);
	dbg_print("proc_create: proc created\n");
	return proce;
}
/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
	int fd=0;
	vmarea_t *temp=NULL;

	proc_t *cur_first_child, *cur_last_Child, *anchor, *anchor_last_child, *p;
	kthread_t *t_to_wake_up;

    dbg_print("proc_cleanup: closing all open files...\n");
	vput(curproc->p_cwd);
    dbg_print("proc_cleanup: going to close all open files...\n");

	for (fd = 0; fd < NFILES; fd++) {
		/*Check if file has already been closed by a dup fd*/
		if (curproc->p_files[fd] && curproc->p_files[fd]->f_refcount > 0) {
			do_close(fd);
		}
	}
	if(list_empty(&curproc->p_vmmap->vmm_list))
	    dbg_print("VMMAP is empty. No need to iterate\n");

    dbg_print("proc_cleanup: closed all open files...\n");
	list_iterate_begin(&curproc->p_vmmap->vmm_list, temp, vmarea_t,vma_plink )
	{
		dbg_print("iterating thru the llop of vmareas to remove");
		temp->vma_obj->mmo_ops->put(temp->vma_obj);
		list_remove(&temp->vma_plink);
	}list_iterate_end();

	dbgq(DBG_PROC, "proc_cleanup:Cleaning up the process %s......\n", curproc->p_comm);
	/*NOT_YET_IMPLEMENTED("PROCS: proc_cleanup");*/
	KASSERT(NULL != proc_initproc);
	KASSERT(1 <= curproc->p_pid);
	KASSERT(NULL != curproc->p_pproc);

	if(!(list_empty(&curproc->p_children)))
	{
		list_iterate_begin(&curproc->p_children, p, proc_t, p_child_link)
		{
				p->p_pproc = proc_initproc;
				list_insert_tail(&proc_initproc->p_children, &p->p_child_link);
				KASSERT( p != NULL);
				dbgq(DBG_PROC, "Orphan's PID is= %d has been assigned to initproc\n",p->p_pid);
		}list_iterate_end();

	}
	dbgq(DBG_PROC, "Status of %s set to: %d\n", curproc->p_comm,curproc->p_status);
	curproc->p_state= PROC_DEAD;
	sched_wakeup_on(&curproc->p_pproc->p_wait);
   dbgq(DBG_PROC, "State of %s set to: %d\n", curproc->p_comm,curproc->p_state);
	dbg_print("proc_cleanup: Switching the scheduler..\n");

	sched_switch();

    }
/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
    dbg_print("proc_kill: killing proc %d\n",p->p_pid);

	p->p_state=PROC_DEAD;
	p->p_status=status;

	if(curproc==p)
    {
    	do_exit(status);
    }
	else
	{
		kthread_t *temp;
		list_iterate_begin(&curproc->p_threads, temp, kthread_t, kt_plink)
		{
				kthread_cancel(temp,0);
			/*join with them how??????*/
		}list_iterate_end();

	}
    dbg_print("proc_kill: Killed proc %d\n",p->p_pid);
   /*   NOT_YET_IMPLEMENTED("PROCS: proc_kill");*/
}
/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
/*----Kernel1:PROCS:proc_kill_all:Begins---*/
proc_t *proc;
kthread_t *thread;
list_iterate_begin(&_proc_list,proc,proc_t,p_list_link){
if(proc->p_pid!=PID_IDLE && proc->p_pid != PID_INIT && proc->p_pid != 2 && proc != curproc)
{
dbg_print("KILL_ALL calling proc_kill from PID %d on PID %d\n",curproc->p_pid,proc->p_pid);
proc_kill(proc,0);
}
}list_iterate_end();

/*kill the current proc after everybody except 0,1,2 are dead*/
if (curproc->p_pid != PID_INIT)
{
do_exit(0);
}
/*----Ends---*/
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
    dbg_print("proc_thread_exited: A thread of process %d has exited\n",curthr->kt_proc->p_pid);
/*	kthread_destroy(curthr);*/
	proc_cleanup(0);
    dbg_print("proc_thread_exited: Done cleaning up after thread\n");


    /*    NOT_YET_IMPLEMENTED("PROCS: proc_thread_exited");*/
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument,
 * or if all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
	proc_t *p;
	int found;
	kthread_t *thr;
	if (options!=0)
	{
		dbg_print("dowaitpid: Invalid values in options\n");
		/*Insert code to handle error condition here*/
		return 0;/*delete this later.*/
	}
	if (pid!=-1 && pid<1)
	{
		dbg_print("dowaitpid: Invalid pid passed\n");
		/*Insert code to handle error condition here*/
		return 0;/*delete this later.*/
	}
	/*If no children, error case*/
	if (list_empty(&curproc->p_children) == 1)
	{
		dbg_print("dowaitpid: No child found for process %d\n",curproc->p_pid);
		return -ECHILD;
	}

	/*Wait for ANY ONE of child processes to finish*/
	if (pid == -1)
	{
		while (1)
		{
			list_iterate_begin(&curproc->p_children, p,proc_t,p_child_link)
			{
				KASSERT(NULL != p);
				if (p->p_state == PROC_DEAD)
				{
					KASSERT(NULL != p->p_pagedir);
					if(status!=NULL){
					*status = p->p_status;}
					pid=p->p_pid;
					/*destory all process threads*/
					dbg_print("dowaitpid: -1 destroying threads on %d\n",p->p_pid);
					list_iterate_begin(&p->p_threads,thr,kthread_t,kt_plink)
					{
						kthread_destroy(thr);
					}list_iterate_end();

					list_remove(&p->p_child_link);
					dbg_print("dowaitpid:Child process removed from Parent's proc list\n");

					list_remove(&p->p_list_link);
					dbg_print("dowaitpid:Child process removed from global proc list\n");

					return p->p_pid;
				}
			}list_iterate_end();
			sched_sleep_on(&curproc->p_wait);
		}
	}
	else if (pid > 0)
	{
		found=0;
		list_iterate_begin(&curproc->p_children, p , proc_t, p_child_link) {
			KASSERT(NULL != p);
			if(p->p_pid == pid)
			{
				found=1;
				KASSERT(NULL != p->p_pagedir);
				sched_sleep_on(&curproc->p_wait);
				*status = p->p_status;
				pid = p->p_pid;
				/*destory all process threads*/
				list_iterate_begin(&p->p_threads,thr,kthread_t,kt_plink) {
					kthread_destroy(thr);
				}list_iterate_end();

				list_remove(&p->p_child_link);
				dbg_print("dowaitpid:Child process removed from Parent's proc list\n");

				list_remove(&p->p_list_link);
				dbg_print("dowaitpid:Child process removed from global proc list\n");

				return p->p_pid;
			}
		}list_iterate_end();
	}

				/*PID not found in child list*/
	return -ECHILD;
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
	dbg_print("do_exit: Entered\n");

	kthread_t *temp;

	list_iterate_begin(&curproc->p_threads, temp, kthread_t, kt_plink)
	{
		/*Code to cancel all other threads*/
		/*This part not reqd unless MTP*/
    	if (temp!=curthr)
    		kthread_cancel(temp,(void *)status);
    	/*join with them how??????*/

		/*This part not reqd unless MTP*/
	}list_iterate_end();
	/*exit from cur thread*/
	curproc->p_state=PROC_DEAD;
	kthread_exit((void *)status);
	dbg_print("do_exit: Cancelled all threads and exiting\n");

	/*	NOT_YET_IMPLEMENTED("PROCS: do_exit");*/
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}

