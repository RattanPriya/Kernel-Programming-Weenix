#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
* Most of the work should be done by the vnode's implementation
* specific lookup() function, but you may want to special case
* "." and/or ".." here depnding on your implementation.
*
* If dir has no lookup(), return -ENOTDIR.
*
* Note: returns with the vnode refcount on *result incremented.
*/

int lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result) {
    int retval;
    vnode_t *res;

    KASSERT(NULL != dir);
    KASSERT(NULL != name);
    KASSERT(NULL != result);


    /* if node is not a directory */
    if (!S_ISDIR(dir->vn_mode)) {
        dbg(DBG_ERROR | DBG_VFS,
                "namev.c : lookup : vnode %ld is not a directory\n", (long)dir->vn_vno);
        return -ENOTDIR;
    }

/*    vfs_is_in_use(vfs_root_vn->vn_fs);*/
    if (len == 0) {
        *result = vget(dir->vn_fs, dir->vn_vno);
        return 0;
    }
    vget(dir->vn_fs, dir->vn_vno);
    if (len > NAME_LEN) {
        vput(dir);
        dbg(DBG_ERROR | DBG_VFS, "namev.c : lookup : name is too long\n");
        return -ENAMETOOLONG;
    }

    /*vget(base->vn_fs, base->vn_vno);*/
    vfs_is_in_use(vfs_root_vn->vn_fs);
    retval = dir->vn_ops->lookup(dir,name,len,result);
    vput(dir);

    vfs_is_in_use(vfs_root_vn->vn_fs);

    return retval;

}

/* When successful this function returns data in the following "out"-arguments:
* o res_vnode: the vnode of the parent directory of "name"
* o name: the `basename' (the element of the pathname)
* o namelen: the length of the basename
*
* For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
* &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
* vnode corresponding to "/s5fs/bin" in res_vnode.
*
* The "base" argument defines where we start resolving the path from:
* A base value of NULL means to use the process's current working directory,
* curproc->p_cwd. If pathname[0] == '/', ignore base and start with
* vfs_root_vn. dir_namev() should call lookup() to take care of resolving each
* piece of the pathname.
*
* Note: A successful call to this causes vnode refcount on *res_vnode to
* be incremented.
*/

int dir_namev(const char *pathname, size_t *namelen, const char **name,
        vnode_t *base, vnode_t **res_vnode) {
    int err_num = 0;
    int index = 0;
    int l=0, len = 0;
    char *ptr;

    KASSERT(NULL != pathname);
    dbg(DBG_PRINT | DBG_VFS,"DIR_NAMEV: Entered dir_namev, pathname = %s\n", pathname);

    /* if base is null, set it to current pwd */
    if (base == NULL){
    	KASSERT(NULL!=curproc);
        KASSERT(NULL!=curproc->p_cwd);
    	base = curproc->p_cwd;
    }


    /* pathname begins with '/' i.e. it's the root*/
    if (pathname[0] == '/') {
        base = vfs_root_vn;
        index = 1;
    }

    while (1) {
        /* moving pathname so that it doesn't point to '/' */
        for(;l<index;l++){
            pathname++;
        }

        /*  get location of next '/' */
        ptr = strchr(pathname, '/');
        if(NULL!=ptr){
            /* get the length of directory name */
            len=strlen(pathname)-strlen(ptr);
            dbg(DBG_PRINT | DBG_VFS,"Call..........pathname: %s\n", ptr);

            err_num = lookup(base,pathname, len, res_vnode);
            if (err_num < 0)
                return err_num;

            vfs_is_in_use(vfs_root_vn->vn_fs);
            vput(*res_vnode);
            vfs_is_in_use(vfs_root_vn->vn_fs);
            base = *res_vnode;
            index = index + len + 1;
        } else {
            ptr = strchr(pathname, '\0');
            len = strlen(pathname)-strlen(ptr);
            if (len > NAME_LEN) {
            	dbg(DBG_PRINT | DBG_VFS,"name is too long\n");
                return -ENAMETOOLONG;
            }
            KASSERT(NULL != base);
            *res_vnode = vget(base->vn_fs, base->vn_vno);
            KASSERT(NULL != *res_vnode);
            if (!S_ISDIR((*res_vnode)->vn_mode)) {
            	dbg(DBG_PRINT | DBG_VFS,"is not a directory\n");
                vput(*res_vnode);
                return -ENOTDIR;
            }

            *name = &pathname[0];
            *namelen = len;
            return 0;
        }
    }
    KASSERT(NULL != res_vnode);
    KASSERT(NULL != name);
    KASSERT(NULL != namelen);
    dbg(DBG_PRINT | DBG_VFS,"returning\n");

    return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
* It makes use of dir_namev and lookup to find the specified vnode (if it
* exists). flag is right out of the parameters to open(2); see
* <weenix/fnctl.h>. If the O_CREAT flag is specified, and the file does
* not exist call create() in the parent directory vnode.
*
* Note: Increments vnode refcount on *res_vnode.
*/

int open_namev(const char *pathname, int flag, vnode_t **res_vnode,
        vnode_t *base) {

    size_t len;
    const char *file_name;
    int retval = 0;
    KASSERT( NULL != pathname);
    KASSERT( NULL != res_vnode);

    vnode_t **result=NULL;

    retval = dir_namev(pathname, &len, &file_name, base, res_vnode);
    dbg(DBG_PRINT | DBG_VFS,"\n\nretval : %d \n",retval);
    dbg(DBG_PRINT | DBG_VFS,"\n\nflag : %d \n",flag);
	if (retval < 0) {
		return retval;
	}
	vput(*res_vnode);
	retval = lookup(*res_vnode, file_name, len, res_vnode);
	if ((flag & O_CREAT) == O_CREAT && retval == -ENOENT) {
		/*Create the file*/
		KASSERT(NULL != (*res_vnode)->vn_ops->create);
		dbg(DBG_ERROR | DBG_VFS,
				"open_namev, lookup failed on name = %s, created\n", file_name);

		retval = (*res_vnode)->vn_ops->create(*res_vnode, file_name, len,
				res_vnode);
	}

	dbg(DBG_PRINT | DBG_VFS, "\n\nnamev.c: open_namev: Leaving open_namev");

	/*NOT_YET_IMPLEMENTED("VFS: open_namev");*/
	/*Returning without any problems*/
	return retval;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
* to the given buffer. On success 0 is returned. If 'dir' does not
* contain 'entry' then -ENOENT is returned. If the given buffer cannot
* hold the result then it is filled with as many characters as possible
* and a null terminator, -ERANGE is returned.
*
* Files can be uniquely identified within a file system by their
* inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
* directories cannot have more than one link there is always
* a unique solution. The path is writen to the given buffer.
* On success 0 is returned. On error this function returns a
* negative error code. See the man page for getcwd(3) for
* possible errors. Even if an error code is returned the buffer
* will be filled with a valid string which has some partial
* information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */

