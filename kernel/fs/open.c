/*
 * FILE: open.c
 * AUTH: mcc | jal
 * DESC:
 * DATE: Mon Apr 6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"
#include "fs/lseek.h"

/* find empty index in p->p_files[] */
int get_empty_fd(proc_t *p) {
	int fd;

	for (fd = 0; fd < NFILES; fd++) {
		if (!p->p_files[fd])
			return fd;
	}

	dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
	"for pid %d\n", curproc->p_pid);
	return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 * 1. Get the next empty file descriptor.
 * 2. Call fget to get a fresh file_t.
 * 3. Save the file_t in curproc's file descriptor table.
 * 4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 * oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 * O_APPEND.
 * 5. Use open_namev() to get the vnode for the file_t.
 * 6. Fill in the fields of the file_t.
 * 7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EINVAL
 * oflags is not valid.
 * o EMFILE
 * The process already has the maximum number of files open.
 * o ENOMEM
 * Insufficient kernel memory was available.
 * o ENAMETOOLONG
 * A component of filename was too long.
 * o ENOENT
 * O_CREAT is not set and the named file does not exist. Or, a
 * directory component in pathname does not exist.
 * o EISDIR
 * pathname refers to a directory and the access requested involved
 * writing (that is, O_WRONLY or O_RDWR is set).
 * o ENXIO
 * pathname refers to a device special file and no corresponding device
 * exists.
 */

int do_open(const char *filename, int oflags) {
	vnode_t *res_vnode;
	int status_flag = 0, access_flag = 0, fd = 0, errno= 0;

	dbg(DBG_PRINT | DBG_VFS,
			"Kernel2:SysMsg: begin do_open(), filename =%s\n", filename);

	/*Error cases*/
	if (strlen(filename) > MAXPATHLEN) {

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - The component of the Filename too long\n", filename);
		return -ENAMETOOLONG;
	}

	if (strlen(filename) < 1) {

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - Invalid file as length is less than 1 \n", filename);
		return -EINVAL;
	}

	/*set up the access and status flags*/
	status_flag = oflags & 0x700; /*O_CREAT,O_TRUNC,O_APPEND*/
	access_flag = oflags & 0x003; /*O_RDONLY,O_WRONLY,O_RDWR*/

	if (access_flag != O_RDONLY && access_flag != O_WRONLY
			&& access_flag != O_RDWR) {

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - Invalid file\n", filename);
		return -EINVAL;
	}

	/* get empty file descriptor, open file,O_CREAT handled in open_namev */
	dbg(DBG_PRINT | DBG_VFS,"\n\n\ngetting file descriptor \n");
	fd = get_empty_fd(curproc);

	if (fd < 0) {

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - Maximum number of files open\n", filename);
		return -EMFILE;
	}

	curproc->p_files[fd] = fget(-1);

	if (curproc->p_files[fd] == NULL) {

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - Insufficient Kernel memory\n", filename);
		return -ENOMEM;
	}

	dbg(DBG_PRINT | DBG_VFS,"\n\n\ncalling open_namev \n");
	errno = open_namev(filename, status_flag, &res_vnode, NULL);
	if (errno < 0) {
		fput(curproc->p_files[fd]);
		return errno;
	}

	/* Check error for opening a dir for write*/
	if (S_ISDIR(res_vnode->vn_mode)
			&& (access_flag == O_RDWR || access_flag == O_WRONLY)) {
		dbg(DBG_PRINT | DBG_VFS,"\n\n\ncalling fput and vput \n");
		fput(curproc->p_files[fd]);
		vput(res_vnode);

		dbg(DBG_ERROR | DBG_VFS,
				"Kernel2:SysMsg: in do_open(), filename =%s - The pathname is a directory or the access requested requires writing\n", filename);
		return -EISDIR;
	}

	/* Make O_TRUNC behavior default */
	if (errno
			== 0&& (status_flag & O_APPEND) != O_APPEND && access_flag == O_WRONLY) {vput(res_vnode);
			dbg(DBG_PRINT | DBG_VFS,"\n\n\nunlinking..... \n");
	errno = do_unlink(filename);
	if ( errno < 0 )
	{
		fput(curproc->p_files[fd]);
		return errno;
	}
	errno = open_namev(filename,O_CREAT,&res_vnode,NULL);
	if ( errno < 0 )
	{
		fput(curproc->p_files[fd]);
		return errno;
	}
}

	curproc->p_files[fd]->f_vnode = res_vnode;

	/*Handle file O_APPEND or O_TRUNC*/
	switch (status_flag) {
	case O_APPEND:
		curproc->p_files[fd]->f_mode = FMODE_APPEND;
		break;
	case O_APPEND | O_CREAT:
		curproc->p_files[fd]->f_mode = FMODE_APPEND;
		break;
	default:
		curproc->p_files[fd]->f_mode = 0;
		curproc->p_files[fd]->f_pos = 0;
		break;
	}

	/*Handle file access*/
	switch (access_flag) {
	case O_RDONLY:
		curproc->p_files[fd]->f_mode |= FMODE_READ;
		break;
	case O_WRONLY:
		curproc->p_files[fd]->f_mode |= FMODE_WRITE;
		break;
	case O_RDWR:
		curproc->p_files[fd]->f_mode |= FMODE_WRITE | FMODE_READ;
		break;
	}

	dbg(DBG_PRINT | DBG_VFS, "\n\n\nreturning from do_open \n");
	return fd;
}

