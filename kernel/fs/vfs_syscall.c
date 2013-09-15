/*
 * FILE: vfs_syscall.c
 * AUTH: mcc | jal
 * DESC:
 * DATE: Wed Apr 8 02:46:19 1998
 * $Id: vfs_syscall.c,v 1.1 2012/10/10 20:06:46 william Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/* To read a file:
 * o fget(fd)
 * o call its virtual read f_op
 * o update f_pos
 * o fput() it
 * o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * fd is not a valid file descriptor or is not open for reading.
 * o EISDIR
 * fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int do_read(int fd, void *buf, size_t nbytes) {
	/*NOT_YET_IMPLEMENTED("VFS: do_read");*/
	/*Check if fd is EBADF or EISDIR?? How to check??*/

	int bytesRead = 0;
	file_t *myfile;
	if (fd < 0 || fd >= NFILES/*|| myfile->f_mode!=FMODE_READ*/) {
		dbg(DBG_ERROR,
				"\nDO_READ: Error : Invalid fd / File cannot be read since fget failed. FD: %d", fd);
		return -EBADF;
	}
	myfile = fget(fd);
	if (myfile == NULL) {
		dbg(DBG_ERROR, "Error: fd is not valid file descriptor\n");
		return -EBADF;
	}
	if ((myfile->f_mode & FMODE_READ) != FMODE_READ) {
		dbg(DBG_ERROR, "Error: file is not in read mode\n");
		fput(myfile);
		return -EBADF;
	}
	if (S_ISDIR(myfile->f_vnode->vn_mode)) {
		dbg(DBG_ERROR,
				"\nDO_READ: Error : This is a directory. do_read will not work on a directory");
		fput(myfile);
		return -EBADF;
	}
	KASSERT(NULL!=myfile->f_vnode->vn_ops->read);
	bytesRead = myfile->f_vnode->vn_ops->read(myfile->f_vnode, myfile->f_pos,
			buf, nbytes);
	myfile->f_pos = myfile->f_pos + bytesRead;
	dbg_print("\n\nCalling fput from do_read...............\n\n");
	fput(myfile);
	return bytesRead;
}

/* Very similar to do_read. Check f_mode to be sure the file is writable. If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file. As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * fd is not a valid file descriptor or is not open for writing.
 */
int do_write(int fd, const void *buf, size_t nbytes) {
	/*NOT_YET_IMPLEMENTED("VFS: do_write");*/
	int bytesWrite = 0;
	file_t *myfile;

	dbg(DBG_PRINT | DBG_VFS,
			"\nDO_WRITE: FD received : %d......buf: %s......nbytes: %d\n", fd, (char*)buf, nbytes);
	if (fd < 0 || fd >= NFILES) {
		/*		dbg(DBG_ERROR,"\nDO_WRITE: Error : fd is not a valid file descriptor");*/
		return -EBADF;
	}
	myfile = fget(fd);
	if (NULL == myfile) {
		/*		dbg(DBG_ERROR,"\nDO_WRITE: Error: Bad FD...file is null\n");*/
		return -EBADF;
	}
	if ((myfile->f_mode & FMODE_WRITE) != FMODE_WRITE) {
		dbg_print("\nmode is not write\n");
		fput(myfile);
		return -EBADF;
	}

	if ((myfile->f_mode & FMODE_APPEND) == FMODE_APPEND) {
		myfile->f_pos = do_lseek(fd, 0, SEEK_END);
	}
	bytesWrite = myfile->f_vnode->vn_ops->write(myfile->f_vnode, myfile->f_pos,
			buf, nbytes);
	myfile->f_pos += bytesWrite;
	KASSERT(
			(S_ISCHR(myfile->f_vnode->vn_mode)) || (S_ISBLK(myfile->f_vnode->vn_mode)) ||((S_ISREG(myfile->f_vnode->vn_mode)) && (myfile->f_pos <= myfile->f_vnode->vn_len)));
	dbg_print("\nCalling fput from do_write\n");
	fput(myfile);
	return bytesWrite;
}
/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * fd isn't a valid open file descriptor.
 */
int do_close(int fd) {
	file_t *myfile;
	if (fd < 0 || fd >= NFILES) {
		dbg(DBG_ERROR, "\nDO_CLOSE: Error : Bad file descriptor");
		return -EBADF;
	}

	myfile = fget(fd);
	if (NULL == myfile) {
		dbg(DBG_ERROR, "\nDO_CLOSE: Error : Bad file descriptor");
		return -EBADF;
	}

	dbg(DBG_ERROR, "\ncalling do_close from fput");
	fput(myfile);

	/*       NOT_YET_IMPLEMENTED("VFS: do_close");*/
	curproc->p_files[fd] = NULL;
	fput(myfile);
	dbg(DBG_PRINT | DBG_VFS, "\nDO_CLOSE: Closing the file");
	return 0;
}

/* To dup a file:
 * o fget(fd) to up fd's refcount
 * o get_empty_fd()
 * o point the new fd to the same file_t* as the given fd
 * o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong. Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * fd isn't an open file descriptor.
 * o EMFILE
 * The process already has the maximum number of file descriptors open
 * and tried to open a new one.
 */
int do_dup(int fd) {
	file_t *myfile;
	int fd_index;
	if (fd < 0 || fd >= NFILES) {
		dbg(DBG_ERROR, "\nDO_DUP: Error : Bad file descriptor");
		return -EBADF;
	}
	fd_index = get_empty_fd(curproc);

	if (fd_index == -EMFILE) {
		dbg(DBG_ERROR, "\nDO_DUP: Error: Too many files open");
		return -EMFILE;
	}

	myfile = fget(fd);

	if (NULL == myfile) {
		dbg(DBG_ERROR, "\nDO_DUP: Error: Bad file descriptor");
		return -EBADF;
	}
	curproc->p_files[fd_index] = myfile;
	dbg(DBG_PRINT | DBG_VFS, "\nDO_DUP: Successful.");
	return fd_index;
}/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'. If nfd is in use (and not the same as ofd)
 * do_close() it first. Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * ofd isn't an open file descriptor, or nfd is out of the allowed
 * range for file descriptors.
 */
int do_dup2(int ofd, int nfd) {
	file_t *myfile;
	if (ofd < 0 || ofd >= NFILES || nfd < 0 || nfd >= NFILES) {
		dbg(DBG_ERROR, "\nDO_DUP: Error : Bad file descriptor");
		return -EBADF;
	}
	myfile = fget(ofd);
	if (NULL == myfile) {
		dbg(DBG_ERROR, "\nDO_DUP: Error : Bad file descriptor");
		return -EBADF;
	}
	if (ofd == nfd) {
		fput(myfile);
		return nfd;
	}
	if (curproc->p_files[nfd] && nfd != ofd) {
		dbg(DBG_PRINT | DBG_VFS,
				"\nDO_DUP2: Error: both fds are the same. Closing the file");
		int err_no = do_close(nfd);
		if (err_no < 0) {
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_DUP2: Error: Close did not occur correctly");
			if (NULL != myfile)
				fput(myfile);
			return err_no;
		}
		return nfd;
	}

	curproc->p_files[nfd] = myfile;
	dbg(DBG_PRINT | DBG_VFS, "\nDO_DUP2: Successful.");
	return nfd;

}
/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EINVAL
 * mode requested creation of something other than a device special
 * file.
 * o EEXIST
 * path already exists.
 * o ENOENT
 * A directory component in path does not exist.
 * o ENOTDIR
 * A component used as a directory in path is not, in fact, a directory.
 * o ENAMETOOLONG
 * A component of path was too long.
 */
int do_mknod(const char *path, int mode, unsigned devid) {
	/*Check path */
	size_t namelength;
	vnode_t *res_vnode, *temp;
	const char *name;
	int retval;
	if (mode != S_IFCHR || mode != S_IFCHR || strlen(path) < 1) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_MKNOD: Error: File path too long");
		return -EINVAL;
	}
	if (strlen(path) > MAXPATHLEN) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_MKNOD: Error: File path too long");
		return -ENAMETOOLONG;
	}
	if (strlen(path) < 1) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_MKNOD: Error: File path too short");
		return -EINVAL;
	}
	retval = dir_namev(path, &namelength, &name, NULL, &res_vnode);
	if (retval >= 0) {
		/*temp=res_vnode;*/
		/*		vput(res_vnode);*/
		/* Check if file is already present*/
		if (lookup(res_vnode, name, namelength, &res_vnode) == (-ENOENT)) {
			/*doubt doubt*/
			vput(res_vnode);
			dbg(DBG_PRINT | DBG_VFS, "\nDO_MKNOD: Creating Node..");
			KASSERT(res_vnode->vn_ops->mknod);
			return (res_vnode->vn_ops->mknod(res_vnode, name, namelength, mode,
					devid));
		} else {
			if(retval!=-ENOENT)
				vput(res_vnode);
			dbg(DBG_PRINT | DBG_VFS, "\nDO_MKNOD: Error: File already exists");
			return (-EEXIST);
		}
	} else {
		dbg(DBG_PRINT | DBG_VFS,
				"\nDO_MKNOD: Error: Parent directory not found");
		return retval;
	}

}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in. Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EEXIST
 * path already exists.
 * o ENOENT
 * A directory component in path does not exist.
 * o ENOTDIR
 * A component used as a directory in path is not, in fact, a directory.
 * o ENAMETOOLONG
 * A component of path was too long.
 */
int do_mkdir(const char *path) {
	const char *name;
	size_t len;
	int retval;
	vnode_t *res_vnode;

	if (strlen(path) > MAXPATHLEN) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_MKDIR: Error: File path too long");
		return -ENAMETOOLONG;
	}
	if (strlen(path) < 1) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_MKDIR: Error: File path too short");
		return -EINVAL;
	}

	/*	find parent*/
	retval = dir_namev(path, &len, &name, NULL, &res_vnode);
	if (retval < 0) {
		dbg(DBG_ERROR,
				"\nDO_MKDIR: Error: Parent directory not found");
		vfs_is_in_use(vfs_root_vn->vn_fs);

		return retval;
	}
	vput(res_vnode);

	/* Lookup name and create if fails */
	retval = lookup(res_vnode, name, len, &res_vnode);
	if (retval == -ENOENT) {
		KASSERT(res_vnode->vn_ops->mkdir);
		retval = res_vnode->vn_ops->mkdir(res_vnode, name, len);
		if (retval >= 0) {
			dbg_print(
					"\nDO_MKDIR: Mkdir success! Returning with values: vnode %ld refcount %d\n",
					(long) res_vnode->vn_vno, res_vnode->vn_refcount);
			vfs_is_in_use(vfs_root_vn->vn_fs);

		} else {
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_MKDIR: mkdir failed in vn_ops specific mkdir function. Return value = %d", retval);
			vfs_is_in_use(vfs_root_vn->vn_fs);

		}

		return retval;
	} else if (retval == 0) {
		vput(res_vnode);
		vfs_is_in_use(vfs_root_vn->vn_fs);

		return -EEXIST;
	} else {
		vfs_is_in_use(vfs_root_vn->vn_fs);

		return retval;
	}
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op. The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EINVAL
 * path has "." as its final component.
 * o ENOTEMPTY
 * path has ".." as its final component.
 * o ENOENT
 * A directory component in path does not exist.
 * o ENOTDIR
 * A component used as a directory in path is not, in fact, a directory.
 * o ENAMETOOLONG
 * A component of path was too long.
 */
int do_rmdir(const char *path) {
	size_t namelength;
	vnode_t *res_vnode, *temp;
	const char *name;
	int retval;
	int check_further = 0;
	int notemptyflag = 0, invalflag = 0;
	int len = strlen(path);
	if ((path[len - 2] == '.' && path[len - 1] == '.')
			|| (path[len - 3] == '.' && path[len - 2] == '.'
					&& path[len - 1] == '/')
			|| (path[len - 3] == '/' && path[len - 2] == '.'
					&& path[len - 1] == '.')) {
		notemptyflag = 1;
		if (len <= 3) {
			dbg(DBG_PRINT | DBG_VFS, "\nDO_RMDIR: Error: Cannot delete ..");
			return -ENOTEMPTY;
		}
	} else if ((path[len - 1] == '.')
			|| (path[len - 2] == '.' && path[len - 1] == '/')
			|| (path[len - 2] == '/' && path[len - 1] == '.')) {
		invalflag = 1;
		if (len <= 2) {
			dbg(DBG_PRINT | DBG_VFS, "\nDO_RMDIR: Error: Cannot delete .");
			return -EINVAL;
		}
	}
	retval = dir_namev(path, &namelength, &name, NULL, &res_vnode);
	if (retval >= 0) {
		vput(res_vnode);
		if (invalflag)
			return -EINVAL;
		if (notemptyflag)
			return -ENOTEMPTY;
		KASSERT(res_vnode->vn_ops->rmdir);
		retval = res_vnode->vn_ops->rmdir(res_vnode, name, namelength);
		/* Calling rmdir function */

		if (retval >= 0)
			dbg(DBG_PRINT | DBG_VFS, "\nDO_RMDIR:  Success removed %s", path);
		else
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_RMDIR: Error: Could not remove %s", path);

		return retval;
	} else {
		/*
		 if(invalflag)
		 return -EINVAL;
		 if(notemptyflag)
		 return -ENOTEMPTY;
		 */
		return retval;
	}
	/*    NOT_YET_IMPLEMENTED("VFS: do_rmdir");*/
	return -1;
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EISDIR
 * path refers to a directory.
 * o ENOENT
 * A component in path does not exist.
 * o ENOTDIR
 * A component used as a directory in path is not, in fact, a directory.
 * o ENAMETOOLONG
 * A component of path was too long.
 */
int do_unlink(const char *path) {
	/*Check path */
	/*. and .. check not needed --- files*/
	size_t namelength;
	vnode_t *res_vnode, *parent;
	const char *name;
	int retval = 0;

	if (strlen(path) < 1) {
		return -EINVAL;
	}

	if (strlen(path) > MAXPATHLEN) {
		dbg(DBG_ERROR | DBG_VFS, "DO:UNLINK: path name too long\n");
		return -ENAMETOOLONG;
	}

	retval = dir_namev(path, &namelength, &name, NULL, &parent);
	if (retval < 0) {
		dbg(DBG_PRINT | DBG_VFS,
				"\nDO_UNLINK: Error: Could not find directory Return Value: %d", retval);
		return retval;
	}

	retval = lookup(parent, name, namelength, &res_vnode);
	if (retval < 0) {
		vput(parent);
		return retval;
	}

	if (S_ISDIR(res_vnode->vn_mode)) {
		vput(parent);
		vput(res_vnode);
		dbg(DBG_PRINT | DBG_VFS, "\nDO_UNLINK: Error: This is a directory");
		return -EISDIR;
	}

	dbg(DBG_PRINT | DBG_VFS,
			"\nDO_UNLINK: Unlinking.................name= %s, length = %d", name, namelength);
	KASSERT(parent->vn_ops->unlink!=NULL);
	retval = parent->vn_ops->unlink(parent, name, namelength);
	dbg(DBG_PRINT | DBG_VFS,
			"\nDO_UNLINK: Unlinking successfull.................");
	vput(parent);
	vput(res_vnode);

	return retval;

}

/* To link:
 * o open_namev(from)
 * o dir_namev(to)
 * o call the destination dir's (to) link vn_ops.
 * o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EEXIST
 * to already exists.
 * o ENOENT
 * A directory component in from or to does not exist.
 * o ENOTDIR
 * A component used as a directory in from or to is not, in fact, a
 * directory.
 * o ENAMETOOLONG
 * A component of from or to was too long.
 */
int do_link(const char *from, const char *to) {
	size_t namelength_from;
	size_t namelength_to;
	vnode_t *res_vnode_from;
	vnode_t *res_vnode_to;
	const char *name_from;
	const char *name_to;
	int retval;
	if ((strlen(to) < 1) || strlen(from) < 1) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_LINK: Error: Pathnames too short");
		return -EINVAL;
	}

	if (strlen(from) > MAXPATHLEN || strlen(to) > MAXPATHLEN) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_LINK: Error: Pathnames too long");
		return -ENAMETOOLONG;
	}

	/* DOUBT: FILE OR FOLDER, Current implementation does link a file to a folder, Do we have to do folder to folder or both ?*/
	dbg_print("\ndo_link: calling open_namev\n");
	retval = open_namev(from, 0, &res_vnode_from, NULL);
	if (retval >= 0) {
		dbg_print("\ndo_link: calling dir_namev\n");
		retval = dir_namev(to, &namelength_to, &name_to, NULL, &res_vnode_to);
		if (retval >= 0) {
			dbg_print("\ndo_link: calling vput\n");
			vput(res_vnode_to);

			retval = lookup(res_vnode_to, name_to, namelength_to,
					&res_vnode_to);
			if (retval == 0) {
				vput(res_vnode_to);
				vput(res_vnode_from);
				dbg(DBG_PRINT | DBG_VFS,
						"\nDO_LINK: Error: Dest already exists");
				return -EEXIST;
			} else if (retval == -ENOENT) {
				retval = res_vnode_to->vn_ops->link(res_vnode_from,
						res_vnode_to, name_to, namelength_to);
				vput(res_vnode_from);
				dbg(DBG_PRINT | DBG_VFS,
						"\nDO_LINK: Returning after successful link");
				return retval;
			} else {
				vput(res_vnode_from);
				dbg(DBG_PRINT | DBG_VFS,
						"\nDO_LINK: Error: Source file not found.");
				return retval;
			}
		} else {

			vput(res_vnode_from);
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_LINK: Error: Source file not found.");
			return retval;
		}
	} else {
		return retval;
	}

}

/* o link newname to oldname
 * o unlink oldname
 * o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int do_rename(const char *oldname, const char *newname) {
	int retval = 0;
	/*
	 DOUBT: from, to is old,new ---> as in param names or
	 or new, old as in the comments above.
	 */

	dbg(DBG_PRINT | DBG_VFS, "do_rename: Entered\n");

	if (strlen(oldname) > 1 && strlen(newname) > 1) {
		dbg_print("\n do_rename: oldname: %s  newname: %s \n", oldname,
				newname);
		retval = do_link(oldname, newname);
		/*retval=do_unlink(oldname);*/
		dbg(DBG_PRINT | DBG_VFS,
				"do_rename: rename Successful, do_rename returning\n");
		return retval;
	} else {
		dbg(DBG_ERROR | DBG_VFS,
				"do_rename: rename Failed. Reason: Invalid arguments \n");
		return -EINVAL;
	}
	/*
	 NOT_YET_IMPLEMENTED("VFS: do_rename");
	 return -1;
	 */
}

/* Make the named directory the current process's cwd (current working
 * directory). Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 * o ENOENT
 * path does not exist.
 * o ENAMETOOLONG
 * A component of path was too long.
 * o ENOTDIR
 * A component of path is not a directory.
 */
int do_chdir(const char *path) {
	int retval = 0;
	vnode_t *res_vnode;
	const char *name;
	size_t namelength;
	int len = 0;
	if (strlen(path) < 1) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_CHDIR: Error: Pathname is too short");
		return -EINVAL;
	}

	if (strlen(path) > MAXPATHLEN) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_CHDIR: Error: Pathname is too long");
		return -ENAMETOOLONG;
	}

	len = strlen(path);
	if ((path[len - 1] == '.' && path[len - 2] != '.')
			|| (path[len - 3] != '.' && path[len - 2] == '.'
					&& path[len - 1] == '/')
			|| (path[len - 2] == '/' && path[len - 1] == '.')) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_CHDIR: Switching to parent directory");
		/*		if(curproc->p_cwd)*/
		vput(curproc->p_cwd);
		curproc->p_cwd = vfs_root_vn;
		vget(curproc->p_cwd->vn_fs,curproc->p_cwd->vn_vno);
	}

	else
	{
		dbg(DBG_PRINT | DBG_VFS,
				"\n\nDO_CHDIR: calling open_namev......          path :%s        \n\n", path);

		retval = open_namev(path, 0, &res_vnode, NULL);
		if (retval < 0) {
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_CHDIR: Error: Could not find dir given");
			return -ENOENT;
		}

		if (!S_ISDIR(res_vnode->vn_mode)) {
			dbg(DBG_PRINT | DBG_VFS,
					"\n\nDO_CHDIR: pathname is not a directory......          path :%s        \n\n", path);
			vput(res_vnode);
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_CHDIR: Error: Pathname does not represent a dir.");
			return -ENOTDIR;
		}
		dbg(DBG_PRINT | DBG_VFS,
				"\n\nDO_CHDIR: pathname is a directory......          path :%s        \n\n", path);
		/*		if(!name_match("..",path,strlen(path))&&!name_match("../",path,strlen(path)))*/
		vput(curproc->p_cwd);
		dbg(DBG_PRINT | DBG_VFS, "\nDO_CHDIR: Successful. Returning..");
		curproc->p_cwd = res_vnode;
	}
	return 0;
	/*------------------------------------------------*/

}
/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t. You need to increment the
 * file_t's f_pos by this amount. As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * Invalid file descriptor fd.
 * o ENOTDIR
 * File descriptor does not refer to a directory.
 */
int do_getdent(int fd, struct dirent *dirp) {

	int retval;
	file_t *myfile;
	size_t *namelen;
	char *name;
	vnode_t *base;
	vnode_t *res_vnode;

	if (fd < 0 || fd > NFILES) {
		dbg(DBG_ERROR, "\nDO_GETDENT: Error : Bad file descriptor");
		return -EBADF;
	}
	myfile = fget(fd);

	if (!myfile)
		return -EBADF;

	fput(myfile);
	if (myfile->f_vnode->vn_ops->readdir == NULL) {
		dbg(DBG_PRINT | DBG_VFS, "\nDO_GETDENT: Not a directory");
		return -ENOTDIR;
	} else {
		retval = myfile->f_vnode->vn_ops->readdir(myfile->f_vnode,
				myfile->f_pos, dirp);
		if (retval <= 0) {
			dbg(DBG_PRINT | DBG_VFS,
					"\nDO_GETDENT:Failed at vn_ops function: returning %d", retval);
			return retval;
		}
		if (retval == 0) {
			dbg(DBG_PRINT | DBG_VFS, "\nDO_GETDENT:Successful.. Returning");
			return 0;
		}
		myfile->f_pos = myfile->f_pos + retval;
		dbg(DBG_PRINT | DBG_VFS, "\nDO_GETDENT:Successful.. Returning");

		return sizeof(*dirp);
	}
	return 0;
	/*
	 NOT_YET_IMPLEMENTED("VFS: do_getdent");
	 return -1;
	 */
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 * o EBADF
 * fd is not an open file descriptor.
 * o EINVAL
 * whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 * file offset would be negative.
 */
int do_lseek(int fd, int offset, int whence) {
	dbg(DBG_PRINT | DBG_VFS, "\ndo_lseek: Entered");
	file_t *myfile;
	if (fd < 0 || fd > NFILES) {
		dbg(DBG_ERROR, "\nDO_LSEEK: Error : Bad file descriptor");
		return -EBADF;
	}
	myfile = fget(fd);
	if (NULL == myfile) {
		dbg(DBG_ERROR, "\ndo_lseek: fd is invalid.");
		return -EBADF;
	}
	fput(myfile);
	if (whence == SEEK_SET) {
		if (offset < 0) {
			dbg(DBG_ERROR, "\ndo_lseek: Invalid resulting offset value.");
			return -EINVAL;
		}
		myfile->f_pos = offset;
	} else if (whence == SEEK_END) {
		if (((myfile->f_vnode->vn_len) + offset) < 0) {
			dbg(DBG_ERROR, "\ndo_lseek: Invalid resulting offset value.");
			return -EINVAL;
		}
		myfile->f_pos = ((myfile->f_vnode->vn_len) + offset);
	} else if (whence == SEEK_CUR) {
		if (myfile->f_pos + offset < 0) {
			dbg(DBG_ERROR, "\ndo_lseek: Invalid resulting offset value.");
			return -EINVAL;
		}
		myfile->f_pos = myfile->f_pos + offset;
	} else {
		dbg(DBG_ERROR, "\ndo_lseek: whence value is invalid.");
		return -EINVAL;
	}

	dbg(DBG_PRINT | DBG_VFS, "\ndo_lseek: Leaving function..");
	return myfile->f_pos;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 * o ENOENT
 * A component of path does not exist.
 * o ENOTDIR
 * A component of the path prefix of path is not a directory.
 * o ENAMETOOLONG
 * A component of path was too long.
 */
int do_stat(const char *path, struct stat *buf) {
	int mystat = 0;
	int retval = 0;
	size_t len;
	const char *name;
	vnode_t base;
	vnode_t *res_vnode;
	dbg(DBG_PRINT | DBG_VFS, "\ndo_stat: Stat for %s", path);

	if (strlen(path) < 1)
		return -EINVAL;
	if (strlen(path) > MAXPATHLEN)
		return -ENAMETOOLONG;

	retval = dir_namev(path, &len, &name, NULL, &res_vnode);

	if (retval == -ENOTDIR) {
		dbg(DBG_PRINT | DBG_VFS,
				"\ndo_stat: A component in the path prefix was not a directory.");
		return -ENOTDIR;
	} else if (retval < 0) {/*modified*/
		return retval;
	}

	else if (retval == -ENAMETOOLONG) {
		dbg(DBG_PRINT | DBG_VFS,
				"\ndo_stat: The name of a component in the pathname was too long");
		return -ENAMETOOLONG;
	}
	vput(res_vnode);

	retval = lookup(res_vnode, name, len, &res_vnode);
	if (retval == -ENOENT) {
		dbg(DBG_PRINT | DBG_VFS, "\ndo_Stat:Lookup failed on name\n");
		return -ENOENT;

	}
	if (retval == -ENOTDIR) {
		dbg(DBG_PRINT | DBG_VFS,
				"\ndo_stat: A component along the path was not a directory.");
		return -ENOTDIR;
	} else if (retval == -ENAMETOOLONG) {
		dbg(DBG_PRINT | DBG_VFS,
				"\ndo_stat: The name of one of the components in the pathname was too long");
		return -ENAMETOOLONG;
	}
	vput(res_vnode);
	if (res_vnode->vn_ops->stat != NULL){
		KASSERT(res_vnode->vn_ops->stat);
		mystat = res_vnode->vn_ops->stat(res_vnode, buf);
	}
	else {
		dbg(DBG_PRINT | DBG_VFS, "\ndo_stat: stat function not found.");
		retval = -1;
	}

	dbg(DBG_PRINT | DBG_VFS, "\ndo_stat: Returning..");

	return mystat;
}
#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
	NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
	return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
	NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
	return -EINVAL;
}
#endif

