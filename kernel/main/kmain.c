	#define NO_SELF_TEST 0
	#define PID_TEST 1
	#define PROC_CHLD_TEST 2
	#define STRESS_TEST 3
	#define KILL_TEST 4
	#define DEADLOCK_TEST 5
	#define PROD_CON_TEST 6
	#define KSHELL_TEST 7

	#include "errno.h"
	#include "limits.h"

	#include "types.h"
	#include "globals.h"
	#include "kernel.h"

	#include "util/gdb.h"
	#include "util/init.h"
	#include "util/debug.h"
	#include "util/string.h"
	#include "util/printf.h"
	#include "test/kshell/kshell.h"

	#include "mm/mm.h"
	#include "mm/page.h"
	#include "mm/pagetable.h"
	#include "mm/pframe.h"
	#include "mm/kmalloc.h"

	#include "vm/vmmap.h"
	#include "vm/shadow.h"
	#include "vm/anon.h"

	#include "main/acpi.h"
	#include "main/apic.h"
	#include "main/interrupt.h"
	#include "main/cpuid.h"
	#include "main/gdt.h"

	#include "proc/sched.h"
	#include "proc/proc.h"
	#include "proc/kthread.h"

	#include "drivers/dev.h"
	#include "drivers/blockdev.h"
	#include "drivers/tty/virtterm.h"

	#include "api/exec.h"
	#include "api/syscall.h"

	#include "fs/vfs.h"
	#include "fs/vnode.h"
	#include "fs/vfs_syscall.h"
	#include "fs/fcntl.h"
	#include "fs/stat.h"

	#include "test/kshell/kshell.h"
	#include "test/usertest.h"

	GDB_DEFINE_HOOK(boot)
	GDB_DEFINE_HOOK(initialized)
	GDB_DEFINE_HOOK(shutdown)

	static int self_test = NO_SELF_TEST;
	static void *bootstrap(int arg1, void *arg2);
	static void *idleproc_run(int arg1, void *arg2);
	static kthread_t *initproc_create(void);
	static void *initproc_run(int arg1, void *arg2);
	static void hard_shutdown(void);
	extern int vfstest_main(int argc, char **argv);

	static context_t bootstrap_context;

	void test1();
	void test2();

	/**
	 * This is the first real C function ever called. It performs a lot of
	 * hardware-specific initialization, then creates a pseudo-context to
	 * execute the bootstrap function in.
	 */
	void kmain() {
		GDB_CALL_HOOK(boot);

		dbg_init();
		dbgq(DBG_CORE, "Kernel binary:\n");
		dbgq(DBG_CORE, "  text: 0x%p-0x%p\n", &kernel_start_text, &kernel_end_text);
		dbgq(DBG_CORE, "  data: 0x%p-0x%p\n", &kernel_start_data, &kernel_end_data);
		dbgq(DBG_CORE, "  bss:  0x%p-0x%p\n", &kernel_start_bss, &kernel_end_bss);

		page_init();

		pt_init();
		slab_init();
		pframe_init();

		acpi_init();
		apic_init();
		intr_init();

		gdt_init();

		/* initialize slab allocators */
	#ifdef __VM__
		anon_init();
		shadow_init();
	#endif
		vmmap_init();
		proc_init();
		kthread_init();

	#ifdef __DRIVERS__
		bytedev_init();
		blockdev_init();
	#endif

		void *bstack = page_alloc();
		pagedir_t *bpdir = pt_get();
		KASSERT(NULL != bstack && "Ran out of memory while booting.");
		context_setup(&bootstrap_context, bootstrap, 0, NULL, bstack, PAGE_SIZE,
				bpdir);
		context_make_active(&bootstrap_context);

		panic("\nReturned to kmain()!!!\n");
	}

	/**
	 * This function is called from kmain, however it is not running in a
	 * thread context yet. It should create the idle process which will
	 * start executing idleproc_run() in a real thread context.  To start
	 * executing in the new process's context call context_make_active(),
	 * passing in the appropriate context. This function should _NOT_
	 * return.
	 *
	 * Note: Don't forget to set curproc and curthr appropriately.
	 *
	 * @param arg1 the first argument (unused)
	 * @param arg2 the second argument (unused)
	 */
	static void *
	bootstrap(int arg1, void *arg2) {

		dbg(DBG_PRINT | DBG_VFS, "\n-----BOOTSTRAP FUNCTION ENTERED----\n");
		pt_template_init();
		dbg(DBG_PRINT | DBG_VFS, "ootstrap: pt_template_init successful\n");
		curproc = proc_create("idle_proc");

		dbg(DBG_PRINT | DBG_VFS, "bootstrap:Idle Proc created\n");
		curthr = kthread_create(curproc, idleproc_run, 0, NULL );
		dbg(DBG_PRINT | DBG_VFS, "bootstrap:Idle thread created\n");
		KASSERT(NULL != curproc);
		KASSERT(PID_IDLE == curproc->p_pid);
		KASSERT(NULL != curthr);

		dbg(DBG_PRINT | DBG_VFS, "bootstrap:Idle Proc context made active\n");
		context_make_active(&curthr->kt_ctx);

		panic("weenix returned to bootstrap()!!! BAD!!!\n");
		return NULL ;
	}

	/**
	 * Once we're inside of idleproc_run(), we are executing in the context of the
	 * first process-- a real context, so we can finally begin running
	 * meaningful code.
	 *
	 * This is the body of process 0. It should initialize all that we didn't
	 * already initialize in kmain(), launch the init process (initproc_run),
	 * wait for the init process to exit, then halt the machine.
	 *
	 * @param arg1 the first argument (unused)
	 * @param arg2 the second argument (unused)
	 */
	static void *
	idleproc_run(int arg1, void *arg2) {
		int status;
		pid_t child;
        fs_t *fs;
        vnode_t *vn;

		/* create init proc */
		dbg_print("IdleProc: creating initproc\n");
		kthread_t *initthr = initproc_create();

		init_call_all();

		GDB_CALL_HOOK(initialized);

		/* Create other kernel threads (in order) */

	#ifdef __VFS__
		/* Once you have VFS remember to set the current working directory
		 * of the idle and init processes */

		/* Here you need to make the null, zero, and tty devices using mknod */
		/* You can't do this until you have VFS, check the include/drivers/dev.h
		 * file for macros with the device ID's you will need to pass to mknod */

		/* calling mkdir*/
		dbg(DBG_PRINT | DBG_VFS,"creating directories.............................................\n");
		KASSERT(NULL!=vfs_root_vn);
		dbg(DBG_PRINT | DBG_VFS,"vfs_root_vn is not null.............................................\n");
		curproc->p_cwd = vfs_root_vn;
		KASSERT(NULL!=curproc->p_cwd);
		dbg(DBG_PRINT | DBG_VFS,"curproc i.e. idleproc cwd is not null.............................................\n");
		initthr->kt_proc->p_cwd = vfs_root_vn;
		vref(vfs_root_vn);
		do_mkdir("/dev");
		do_mknod("/dev/null", S_IFCHR, MKDEVID(1,0));
		do_mknod("/dev/zero",S_IFCHR, MKDEVID(1,1));
		do_mknod("/dev/tty0",S_IFCHR, MKDEVID(2,0));
		do_mkdir("/dev/kvm");
		do_mkdir("/dev/test");
		do_mkdir("/dev/test/test1");
		do_mkdir("/dev/test/test2");
		vfs_is_in_use(vfs_root_vn->vn_fs);

	#endif

		/* Finally, enable interrupts (we want to make sure interrupts
		 * are enabled AFTER all drivers are initialized) */
		intr_enable();

		/* Run initproc */
		sched_make_runnable(initthr);
		/* Now wait for it */
		child = do_waitpid(-1, 0, &status);

		KASSERT(PID_INIT == child);

	#ifdef __MTP__
		kthread_reapd_shutdown();
	#endif

	#ifdef __VFS__
		/* Shutdown the vfs: */
		dbg_print("weenix: vfs shutdown...\n");
		vput(curproc->p_cwd);
		if (vfs_shutdown())
		{
			panic("vfs shutdown FAILED!!\n");
		}
	#endif

		/* Shutdown the pframe system */
	#ifdef __S5FS__
		pframe_shutdown();
	#endif

		dbg_print("\nweenix: halted cleanly!\n");
		GDB_CALL_HOOK(shutdown);
		hard_shutdown();
		return NULL ;
	}

	/**
	 * This function, called by the idle process (within 'idleproc_run'), creates the
	 * process commonly refered to as the "init" process, which should have PID 1.
	 *
	 * The init process should contain a thread which begins execution in
	 * initproc_run().
	 *
	 * @return a pointer to a newly created thread which will execute
	 * initproc_run when it begins executing
	 */
	static kthread_t *
	initproc_create(void) {
		proc_t *process1;
		kthread_t *process1_thr;

		process1 = proc_create("process1");
		dbg(DBG_PRINT | DBG_VFS, "\nInitProc: initproc created\n");

		KASSERT(NULL!= process1);
		KASSERT(PID_INIT == process1->p_pid);
		/*check not sure*/

		process1_thr = kthread_create(process1, initproc_run, 0, NULL );
		KASSERT(process1_thr!=NULL);

		/*check*/
		dbg(DBG_PRINT | DBG_VFS, "\nInitProc: initproc_thread created\n");

		return process1_thr;
	}

	void func() {
		dbg(DBG_PRINT | DBG_VFS, "func entered\n\n");
	}

	/**
	 * The init thread's function changes depending on how far along your Weenix is
	 * developed. Before VM/FI, you'll probably just want to have this run whatever
	 * tests you've written (possibly in a new process). After VM/FI, you'll just
	 * exec "/bin/init".
	 *
	 * Both arguments are unused.
	 *
	 * @param arg1 the first argument (unused)
	 * @param arg2 the second argument (unused)
	 */

	void test1() {
		dbg(DBG_PRINT | DBG_VFS, "Hello Test Function\n");
		test2();
	}

	void test2() {
		dbg(DBG_PRINT | DBG_VFS, "Hello Test2 Function\n");
	}

	static void* normal_run(int arg1, void *arg2) {
		dbg_print("\nThread of process %d is running\n", curproc->p_pid);
		return NULL ;

	}
	static void*
	kshell_func() {
		int err;
		kshell_t *ksh = NULL;

		ksh = kshell_create(0);

		dbg(DBG_PRINT | DBG_VFS, "\nInitProc_run: User Test output starts here\n");
		kshell_add_command("test1", (void*) test1, "tests something...");
		kshell_add_command("test2", (void*) test2, "tests something else...");
		KASSERT(ksh && "did not create a kernel shell as expected");
		while ((err = kshell_execute_next(ksh)) > 0)
			;
		KASSERT(err == 0 && "kernel shell exited with an error\n");
		kshell_destroy(ksh);
		return NULL ;

	}
	static void*
	test2_thread1_code(int arg1, void *arg2) {
		dbg(DBG_PRINT | DBG_VFS, "I'm in test1_thread1_code.\n");
		return NULL ;
	}
	static void*
	test2_thread2_code(int arg1, void *arg2) {
		dbg(DBG_PRINT | DBG_VFS, "I'm in test2_thread1_code.\n");
		return NULL ;
	}
	static void*
	test2_thread3_code(int arg1, void *arg2) {
		dbg(DBG_PRINT | DBG_VFS, "I'm in test3_thread1_code.\n");
		return NULL ;
	}
	static void*
	deadlock_code(int arg1, void *arg2) {
		kmutex_t mtx;

		kmutex_init(&mtx);

		dbg(DBG_PRINT | DBG_VFS, "Mutex Initialised.\n");
		kmutex_lock(&mtx);

		dbg(DBG_PRINT | DBG_VFS,
				"Thread 1 acquire lock. Now proceeding to acquire it again to cause DEADLOCK\n");
		kmutex_lock(&mtx);
		dbg(DBG_PRINT | DBG_VFS, "Thread 1 acquire lock again.\n");

		dbg(DBG_PRINT | DBG_VFS, "I'm in test3_thread1_code.\n");
		return NULL ;

	}

	/*
	 *-mutex initialized
	 -common resource elements =0
	 elements consumed=0
	 elements produced=0
	 declare max number of elements in resource
	 *
	 *
	 * */
	/**************************************************************************************************/
	/* CONSUMER_PRODUCER FUNCTION                                      */
	/**************************************************************************************************/
	int prod_con_resource = 0;
	kmutex_t resource_mutex;
	int elements_consumed = 0;
	int elements_produced = 0;
	int max_num_elements = 4;
	ktqueue_t producer_queue;
	ktqueue_t consumer_queue;
	int iterp = 0;
	int iterc = 0;

	static void * producer_func(int a, int *b) {
		dbg(DBG_PRINT | DBG_VFS,
				"************ENTERS PRODUCER FUNCTION************************\n");
		while (elements_produced <= max_num_elements) {
			dbg(DBG_PRINT | DBG_VFS,
					"No. of elements produced is less than max_num_elements\n");
			dbg(DBG_PRINT | DBG_VFS, "Acquiring Lock!");
			kmutex_lock(&resource_mutex);
			dbg(DBG_PRINT | DBG_VFS, "Lock Attained!");

			if (prod_con_resource <= max_num_elements) {

				prod_con_resource++;
				dbg(DBG_PRINT | DBG_VFS,
						"Producer: has produced an element.Releasing Lock...\n");
				kmutex_unlock(&resource_mutex);
				dbg(DBG_PRINT | DBG_VFS, "Producer: Lock Released...\n");
				elements_produced++;
				dbg(DBG_PRINT | DBG_VFS,
						"Producer: Total elements produced is : %d\n", elements_produced);
				dbg(DBG_PRINT | DBG_VFS,
						"Producer: Waking up threads in consumer queue\n");
				sched_broadcast_on(&consumer_queue);
				dbg(DBG_PRINT | DBG_VFS, "Producer: Entering Sleep\n");

				sched_sleep_on(&producer_queue);
				if (elements_produced == max_num_elements) {
					break;
				}
				dbg(DBG_PRINT | DBG_VFS, "Producer: Sleep Entered\n");
			} else {
				dbg(DBG_PRINT | DBG_VFS,
						"Producer could not produce an element. Releasing Lock...\n");
				kmutex_unlock(&resource_mutex);
				dbg(DBG_PRINT | DBG_VFS, "Producer Released Lock...\n");
				sched_broadcast_on(&consumer_queue);
				dbg(DBG_PRINT | DBG_VFS,
						"Producer Waking up threads in consumer queue\n");
				dbg(DBG_PRINT | DBG_VFS, "Producer Entering Sleep\n");
				sched_sleep_on(&producer_queue);
				dbg(DBG_PRINT | DBG_VFS, "Producer Sleep Entered\n");

			}

		}
		sched_broadcast_on(&consumer_queue);

		return NULL ;
	}

	static void * consumer_func(int a, int *b) {
		dbg(DBG_PRINT | DBG_VFS,
				"************ENTERS CONSUMER FUNCTION************************\n");
		while (elements_consumed <= max_num_elements) {
			kmutex_lock(&resource_mutex);
			if (prod_con_resource > 0) {

				if (prod_con_resource > 1) {
					prod_con_resource--;
					dbg(DBG_PRINT | DBG_VFS, "Consumer: First Resource consumed\n");

					prod_con_resource--;
					dbg(DBG_PRINT | DBG_VFS,
							"Consumer: Second Resource consumed\n");
					kmutex_unlock(&resource_mutex);
					dbg(DBG_PRINT | DBG_VFS, "Consumer: released lock\n");

					elements_consumed++;
					elements_consumed++;
					dbg(DBG_PRINT | DBG_VFS,
							"Consumer: Total elements consumed: %d \n", elements_consumed);

					sched_broadcast_on(&producer_queue);
					dbg(DBG_PRINT | DBG_VFS,
							"Consumer: Waking up producer threads\n");

					sched_sleep_on(&consumer_queue);
					dbg(DBG_PRINT | DBG_VFS, "Consumer: entered sleep\n");
				}

				else {
					kmutex_unlock(&resource_mutex);
					dbg(DBG_PRINT | DBG_VFS, "Consumer: released lock\n");

					dbg(DBG_PRINT | DBG_VFS,
							"Consumer: Total elements consumed: %d \n", elements_consumed);

					sched_broadcast_on(&producer_queue);
					dbg(DBG_PRINT | DBG_VFS,
							"Consumer: Waking up producer threads\n");

					sched_sleep_on(&consumer_queue);
					dbg(DBG_PRINT | DBG_VFS, "Consumer: entered sleep\n");
					if (elements_produced == max_num_elements) {
						break;
					}
				}
			} else {

				dbg(DBG_PRINT | DBG_VFS, "Consumer: released Lock");

				kmutex_unlock(&resource_mutex);
				dbg(DBG_PRINT | DBG_VFS,
						"Consumer: Waking up threads in Producer queue");

				sched_broadcast_on(&producer_queue);
				dbg(DBG_PRINT | DBG_VFS, "Consumer: Entering Sleep");

				sched_sleep_on(&consumer_queue);
				dbg(DBG_PRINT | DBG_VFS, "Consumer: Process Sleep Entered");
			}

		}
		sched_broadcast_on(&producer_queue);
		return NULL ;
	}

	static void*
	test3_stress_code(int arg1, void *arg2) {
		dbg(DBG_PRINT | DBG_VFS, "I'm in Stress Test code.\n");
		return NULL ;
	}

	static proc_t*
	create_proc_and_thr() {
		proc_t *p;
		kthread_t *t;
		p = proc_create("p2\n");
		dbg(DBG_PRINT | DBG_VFS,
				"Process %d created. This process's parent is %d\n", p->p_pid, p->p_pproc->p_pid);
		kthread_create(p, test3_stress_code, 0, NULL );
		return p;
	}

	static void*
	test_vfs(int arg1, void *arg2) {
		vfstest_main(1, NULL );
		return NULL ;
	}


	static void *
	initproc_run(int arg1, void *arg2) {
		proc_t *process, *p2, *p3;
		proc_t *p1, *p4;
		kthread_t *thread;
		kthread_t *t1;
		kthread_t *t2;
		kthread_t *t3;
		pid_t child;
		char *argv[] = { NULL };
		char *envp[] = { NULL };

		char * dirname;
		int status, i;
		int fd;
		const char *buf = "i hate OS! i hate OS!";
		size_t nbytes = strlen(buf) + 1;
		int nwrote = 0, nread = 0;
		char *readbuf = kmalloc(nbytes * sizeof(char));

		self_test = 25;
		dbg(DBG_PRINT | DBG_VFS, "\nInitProc_run: User Test output start here");
		switch (self_test) {
		case 0:
			break;
		case 1:
			dbg(DBG_PRINT | DBG_VFS,
					"************************Test Case to print PIDs***********************\n");

			process = proc_create("parent1\n");

			dbg(DBG_PRINT | DBG_VFS, "process created...pid: %d\n", process->p_pid);

			thread = kthread_create(process, normal_run, 0, NULL );
			sched_make_runnable(thread);
			dbg(DBG_PRINT | DBG_VFS, "Calling kill all\n");
			proc_kill_all();
			dbg(DBG_PRINT | DBG_VFS, "Killed all processes\n");

			while ((child = do_waitpid(-1, 0, &status)) > 0) {
				dbg(DBG_PRINT | DBG_VFS, "\ninitproc_run got child %d\n\n", child);
			}
			dbg(DBG_PRINT | DBG_VFS,
					"******************************End of test******************************\n");

			break;
		case 2:
			dbg(DBG_PRINT | DBG_VFS,
					"**************Test Case to check Parent Child Relationships*************\n");

			process = proc_create("p1\n");
			dbg(DBG_PRINT | DBG_VFS,
					"Process %d created. This process's parent is %d\n", process->p_pid, process->p_pproc->p_pid);
			t1 = kthread_create(process, test2_thread1_code, 0, NULL );

			p2 = proc_create("p2\n");
			dbg(DBG_PRINT | DBG_VFS,
					"Process %d created. This process's parent is %d\n", p2->p_pid, p2->p_pproc->p_pid);
			t1 = kthread_create(p2, test2_thread2_code, 0, NULL );

			p3 = proc_create("p3\n");
			dbg(DBG_PRINT | DBG_VFS,
					"Process %d created. This process's parent is %d\n", p3->p_pid, p3->p_pproc->p_pid);
			t1 = kthread_create(p3, test2_thread3_code, 0, NULL );

			proc_kill_all();
			dbg(DBG_PRINT | DBG_VFS,
					"******************************End of test********************************\n");
			break;
		case 3:
			dbg(DBG_PRINT | DBG_VFS,
					"\n******************************Stress Test******************************\n");
			for (i = 0; i < 30; i++) {
				create_proc_and_thr();
			}
			proc_kill_all();
			dbg(DBG_PRINT | DBG_VFS,
					"\n******************************End of test******************************\n");
			break;
		case 4:
			dbg(DBG_PRINT | DBG_VFS,
					"\n**************************Kill process test*************************\n\n");
			for (i = 0; i < 10; i++) {
				if (i == 8) {
					p1 = create_proc_and_thr();
				} else {
					create_proc_and_thr();
				}
			}
			proc_kill(p1, 0);
			dbg(DBG_PRINT | DBG_VFS, "\n\nKilling all processes...\n\n");
			proc_kill_all();
			dbg(DBG_PRINT | DBG_VFS,
					"******************************End of test******************************\n");
			break;
		case 5:

			dbg(DBG_PRINT | DBG_VFS,
					"***********************Test for Deadlocks***************************\n");
			p1 = proc_create("p1mutex1");
			dbg(DBG_PRINT | DBG_VFS,
					"Process created. This process's parent is \n");
			t1 = kthread_create(p1, deadlock_code, 0, NULL );
			sched_make_runnable(t1);

			dbg(DBG_PRINT | DBG_VFS, "Thread created.");
			while ((child = do_waitpid(-1, 0, &status)) > 0) {
				dbg(DBG_PRINT | DBG_VFS, "\ninitproc_run got child %d\n\n", child);
			}

			dbg(DBG_PRINT | DBG_VFS,
					"******************************End of test******************************\n");
			break;

		case 6:
			dbg(DBG_PRINT | DBG_VFS,
					"****************TEST CASE FOR CONSUMER PRODUCER PROBLEM***************\n");

			kmutex_init(&resource_mutex);
			sched_queue_init(&producer_queue);
			sched_queue_init(&consumer_queue);
			p1 = proc_create("producer");
			p2 = proc_create("consumer");
			t1 = kthread_create(p1, (void*) producer_func, 0, NULL );
			t2 = kthread_create(p2, (void*) consumer_func, 0, NULL );
			sched_make_runnable(t1);
			sched_make_runnable(t2);
			while ((child = do_waitpid(-1, 0, &status)) > 0) {
				dbg(DBG_PRINT | DBG_VFS, "\ninitproc_run got child %d\n\n", child);

			}

			proc_kill_all();
			dbg(DBG_PRINT | DBG_VFS,
					"******************************End of test******************************\n");
			break;
		case 7:
			kshell_func();
			break;
		case 8:

			fd = do_open("test1.txt", O_CREAT);
			nwrote = do_write(fd, buf, nbytes);
			do_close(fd);

			if (nwrote >= 0) {
				fd = do_open("test1.txt", O_RDONLY);
				dbg(DBG_PRINT | DBG_VFS,
						"\n\n\n\n\n\n\n\nWrote %d bytes\n", nwrote);
				nread = do_read(fd, readbuf, nbytes);
				do_close(fd);
				if (nread >= 0)
					dbg(DBG_PRINT | DBG_VFS,
							"\nRead the following from the file opened : %s\n", readbuf);
				else
					dbg(DBG_PRINT | DBG_VFS,
							"\nWrite failed with code %d\n", nwrote);

				/**renaming**/
				dbg(DBG_PRINT | DBG_VFS, "\n renaming\n");
				do_rename("test1.txt", "test2.txt");
				dbg(DBG_PRINT | DBG_VFS,
						"\n reading from renamed file test2.txt\n");
				fd = do_open("test2.txt", O_RDONLY);
				nread = do_read(fd, readbuf, nbytes);
				do_close(fd);
				if (nread >= 0)
					dbg(DBG_PRINT | DBG_VFS,
							"\nRead the following from the renamed file : %s\n", readbuf);
				else
					dbg(DBG_PRINT | DBG_VFS,
							"\nWrite failed with code %d\n", nwrote);

				dbg_print("\n\nChanging into test1....retval: %d ",
						do_chdir("/dev/test/test1"));
				dbg_print("\n\nChanging into ..   ....retval: %d ", do_chdir(".."));
				dbg_print("\n\nChanging into test2....retval: %d ",
						do_chdir("test2"));
			} else {
				dbg(DBG_PRINT | DBG_VFS, "\nWrite failed with code %d\n", nwrote);
			}
			break;
		case 9:
			dbg(DBG_PRINT | DBG_VFS, "\nStarting VFS tests\n");
			process = proc_create("VFSTEST_RUN");
			t1 = kthread_create(process, test_vfs, 0, NULL );
			sched_make_runnable(t1);
			while ((child = do_waitpid(-1, 0, &status)) > 0) {
				dbg_print("Kernel2:VFS:\t initproc_run got child %d\n", child);
			}
			dbg(DBG_PRINT | DBG_VFS, "\nDone with VFS tests\n");
			break;
		case 10:

			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---BEGIN---: dup test ***************************\n\n");
			do_mkdir("dupTest");
			do_chdir("dupTest");
			int fddup = 0;
			do_open("duptest.txt", O_CREAT);

			while (curproc->p_files[fddup] != NULL ) {
				fddup = fddup + 1;
			}
			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count before do_dup = %d\n",
					fddup);

			do_dup(fddup - 1);

			fddup = 0;
			while (curproc->p_files[fddup] != NULL ) {
				fddup = fddup + 1;
			}

			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count after do_dup = %d\n",
					fddup);
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---END---: Test for dup ***************************\n\n");
			break;
		case 11:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---BEGIN---: Test for dup2 ***************************\n\n");
			do_mkdir("dup2Test");
			do_chdir("dup2Test");
			int fddup2 = 0;
			do_open("dup2test.txt", O_CREAT);

			while (curproc->p_files[fddup2] != NULL ) {
				fddup2 = fddup2 + 1;
			}
			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count before do_dup2 = %d\n",
					fddup2);

			do_dup2(fddup2 - 1, fddup2);

			fddup2 = 0;
			while (curproc->p_files[fddup2] != NULL ) {
				fddup2 = fddup2 + 1;
			}

			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count after do_dup2 = %d\n",
					fddup2);
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---END---: Test for dup2 ***************************\n\n");

			break;
		case 12:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---BEGIN---: Test for rm ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---Description---***************************\n\n1.Will create directory \"rmtest\"\n2.Will then remove it.\n\n");
			do_mkdir("rmtest");
			do_rmdir("rmtest");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---END---: Test for rm ***************************\n\n");
			/*kshell_func();*/
			break;
		case 13:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---BEGIN---: Test for link ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---Description---***************************\n\n1.Will create directory \"linktest\"\n2.Will then cd into \"linktest\".\n3.Will create file \"linktest1.txt\"\n4.Will then write \"test file data\" into \"linktest1.txt\".\n5.Will then link \"linktest1.txt\" and \"linktest2.txt\".\n\n");
			do_mkdir("linktest");
			do_chdir("linktest");
			do_open("linktest1.txt", O_CREAT);
			int f;
			f = 0;

			while (curproc->p_files[f] != NULL ) {
				f = f + 1;
			}
			f = f - 1;

			do_write(f, "test file data", 14);
			do_link("linktest1.txt", "linktest2.txt");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---END---: Test for link ***************************\n\n");
			kshell_func();
			break;
		case 14:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---BEGIN---: Test for unlink ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---Description---***************************\n\n1.Will create directory \"linktest\"\n2.Will then cd into \"linktest\".\n3.Will create file \"linktest1.txt\"\n4.Will then write \"test file data\" into \"linktest1.txt\".\n5.Will then link \"linktest1.txt\" and \"linktest2.txt\".\n6. Will then unlink \"linktest2.txt\"\n\n");
			do_mkdir("linktest");
			do_chdir("linktest");
			do_open("linktest1.txt", O_CREAT);
			int f2;
			f2 = 0;

			while (curproc->p_files[f2] != NULL ) {
				f2 = f2 + 1;
			}
			f2 = f2 - 1;

			do_write(f2, "test file data", 14);
			do_link("linktest1.txt", "linktest2.txt");

			do_unlink("linktest2.txt");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---END---: Test for unlink ***************************\n\n");
			kshell_func();
			break;

		case 15:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n**************VFS TEST CASE ---BEGIN---: Test for deleting non-empty directory*************\n\n");
			int f3;
			f3 = 0;
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE ---Description---***************************\n\n1.Will create directory \"dnedtest\"\n2.Will then cd into \"dnedtest\".\n3.Will create file \"test1.txt\"\n4.Will then write \"test file data\" into \"test1.txt\".\n5.Will then move to parent directory (\").\n6.Will then attempt to delete \"dnedtest\".\n\n");
			do_mkdir("dendtest");
			do_chdir("dendtest");
			do_open("test1.txt", O_CREAT);

			while (curproc->p_files[f3] != NULL ) {
				f3 = f3 + 1;
			}
			f3 = f3 - 1;

			do_write(f3, "test file data", 14);
			do_chdir("..");
			do_rmdir("dendtest");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for deleting non-empty directory ***************************\n\n");
			kshell_func();
			break;

		case 16:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE] ---BEGIN---: Test for Rename***************************\n\n");
			do_mkdir("RenameTest");
			int fd16 = do_open("test1.txt", O_CREAT);
			const char *buf16 = "Hello Rename Test Case.";
			size_t nbytes16 = strlen(buf16) + 1;
			int nwrote16 = 0, nread16 = 0;
			char *readbuf16 = kmalloc(nbytes16 * sizeof(char));
			nwrote16 = do_write(fd16, buf16, nbytes16);
			do_close(fd16);

			if (nwrote16 >= 0) {
				fd16 = do_open("test1.txt", O_RDONLY);
				dbg(DBG_PRINT | DBG_VFS,
						"\n\n\n\n\n\n\n\nWrote %d bytes\n", nwrote16);
				nread16 = do_read(fd16, readbuf16, nbytes16);
				do_close(fd16);
				if (nread16 >= 0)
					dbg(DBG_PRINT | DBG_VFS,
							"\nRead the following from the file opened : %s\n", readbuf16);
				else
					dbg(DBG_PRINT | DBG_VFS,
							"\nWrite failed with code %d\n", nwrote16);

				/**renaming**/
				dbg(DBG_PRINT | DBG_VFS, "\n renaming\n");
				do_rename("test3.txt", "test2.txt"); /* test3.txt does not exist causing failure */
				dbg(DBG_PRINT | DBG_VFS,
						"\n reading from renamed file test2.txt\n");
				fd16 = do_open("test2.txt", O_RDONLY);
				nread16 = do_read(fd16, readbuf16, nbytes16);
				do_close(fd16);
				if (nread16 >= 0)
					dbg(DBG_PRINT | DBG_VFS,
							"\nRead the following from the renamed file : %s\n", readbuf16);
				else
					dbg(DBG_PRINT | DBG_VFS,
							"\nWrite failed with code %d\n", nwrote16);

				do_unlink("test1.txt");

			} else {
				dbg(DBG_PRINT | DBG_VFS, "\nWrite failed with code %d\n", nwrote16);
			}
			kshell_func();
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE] ---END---: Test for Rename***************************\n\n");
			break;

		case 17:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for mkdir ***************************\n\n");
			do_mkdir("..");

			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for mkdir ***************************\n\n");
			kshell_func();
			break;
		case 18:

			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for dup ***************************\n\n");
			do_mkdir("dupTest");
			do_chdir("dupTest");
			int fddup18 = 0;
			do_open("duptest.txt", O_CREAT);

			while (curproc->p_files[fddup18] != NULL ) {
				fddup18 = fddup18 + 1;
			}
			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count before do_dup = %d\n",
					fddup18);

			do_dup(fddup18); /* The Null value of curproc->p_files[fddup] will cause failure of do_dup */

			fddup18 = 0;
			while (curproc->p_files[fddup18] != NULL ) {
				fddup18 = fddup18 + 1;
			}

			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count after do_dup = %d\n",
					fddup18);
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for dup ***************************\n\n");

			break;
		case 19:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for dup2 ***************************\n\n");
			do_mkdir("dup2Test");
			do_chdir("dup2Test");
			int fddup219 = 0;
			do_open("dup2test.txt", O_CREAT);

			while (curproc->p_files[fddup219] != NULL ) {
				fddup219 = fddup219 + 1;
			}
			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count before do_dup2 = %d\n",
					fddup219);

			do_dup2(fddup219, fddup219); /* The Null value of curproc->p_files[fddup2] will cause fnumber of files to remain same after do_dup2 */

			fddup219 = 0;
			while (curproc->p_files[fddup219] != NULL ) {
				fddup219 = fddup219 + 1;
			}

			dbg_print(
					"\n****VFS TEST CASE: Test for dup **** :File count after do_dup2 = %d\n",
					fddup219);
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for dup2 ***************************\n\n");
			kshell_func();
			break;
		case 20:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for rm ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---Description---***************************\n\n1.Will attempt to remove \"..\".\n\n");
			do_rmdir(".."); /* This statement will cause failure. */
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for rm ***************************\n\n");
			kshell_func();

			break;
		case 21:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for link ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---Description---***************************\n\n1.Will create directory \"linktest\"\n2.Will then cd into \"linktest\".\n3.Will create file \"linktest1.txt\"\n4.Will then write \"test file data\" into \"linktest1.txt\".\n5.Will then link \"linktest3.txt\" and \"linktest2.txt\".\n\n");
			do_mkdir("linktest");
			do_chdir("linktest");
			do_open("linktest1.txt", O_CREAT);
			int f21;
			f21 = 0;

			while (curproc->p_files[f21] != NULL ) {
				f21 = f21 + 1;
			}
			f21 = f21 - 1;

			do_write(f21, "test file data", 14);
			do_link("linktest3.txt", "linktest2.txt");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for link ***************************\n\n");
			kshell_func();
			break;
		case 22:
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---BEGIN---: Test for unlink ***************************\n\n");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---Description---***************************\n\n1.Will create directory \"linktest\"\n2.Will then cd into \"linktest\".\n3.Will create file \"linktest1.txt\"\n4.Will then write \"test file data\" into \"linktest1.txt\".\n5.Will then link \"linktest1.txt\" and \"linktest2.txt\".\n6. Will then unlink \"linktest3.txt\"\n\n");
			do_mkdir("linktest");
			do_chdir("linktest");
			do_open("linktest1.txt", O_CREAT);
			int f222;
			f222 = 0;

			while (curproc->p_files[f222] != NULL ) {
				f222 = f222 + 1;
			}
			f222 = f222 - 1;

			do_write(f222, "test file data", 14);
			do_link("linktest1.txt", "linktest2.txt");
			do_unlink("linktest3.txt");
			dbg(DBG_PRINT | DBG_VFS,
					"\n\n***********************VFS TEST CASE [NEGATIVE]---END---: Test for unlink ***************************\n\n");
			kshell_func();
			break;

		case 23:
			dbg_print("TEST CASE ---sbin init\n");

			   kernel_execve("/sbin/init", argv, envp);
			   dbg_print("TEST CASE SBIN/INIT PASSED");
			break;
		case 24:
			dbg_print("TEST CASE ARGS\n");
			argv[0] ="ab";
			argv[1] ="cde";
			argv[2] ="fghi";
			argv[3] ="j";
			kernel_execve("/usr/bin/init", argv, envp);
			dbg_print("TEST CASE ARGS PASSED");
			break;

		case 25:
			dbg_print("TEST CASE SEGFAULT");

			   kernel_execve("/usr/bin/segfault", argv, envp);
			   dbg_print("TEST CASE SEGFAULT PASSED");
			break;


		case 26:
			dbg_print("TEST CASE VFSTEST\n");

			   kernel_execve("/usr/bin/vfstest", argv, envp);
			   dbg_print("TEST CASE VFSTEST PASSED");
			break;
		case 27:
			dbg_print("TEST CASE STRESS\n");

			   kernel_execve("/usr/bin/stress", argv, envp);
			   dbg_print("TEST CASE STRESS PASSED");
			break;
		case 28:
			dbg_print("TEST CASE MEMTEST\n");

			   kernel_execve("/usr/bin/memtest", argv, envp);
			   dbg_print("TEST CASE MEMTEST PASSED");
			break;
		case 29:
			dbg_print("TEST CASE EATMEM\n");

			   kernel_execve("/usr/bin/eatmem", argv, envp);
			   dbg_print("TEST CASE EATMEM PASSED");
			break;
		case 30:
			dbg_print("TEST CASE FORKBOMB\n");

			   kernel_execve("/usr/bin/forkbomb", argv, envp);
			   dbg_print("TEST CASE FORKBOMB PASSED");
			break;

		default:
			dbg_print(
					"\nInvalid value in self_test. \n\n");
			break;
		}
		/*NOT_YET_IMPLEMENTED("PROCS: initproc_run");*/
		dbg(DBG_PRINT | DBG_VFS, "\nInitProc_run: User Test output ends here");

		return NULL ;

	}

	/**
	 * Clears all interrupts and halts, meaning that we will never run
	 * again.
	 */
	static void hard_shutdown() {
	#ifdef __DRIVERS__
		vt_print_shutdown();
	#endif
		__asm__ volatile("cli; hlt");
	}

