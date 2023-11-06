#define FUSE_USE_VERSION 25
#include <fuse_lowlevel.h>
#include <syslog.h>
#include "ext2fs.h"
#include "perms.h"

int check_perms_in_inode(perms_struct ps, struct ext2_inode *inode,
                int perms_requested)
{
    const struct fuse_ctx *ctx = ps;
    int perms = inode->i_mode & 0777;
    // ps = NULL is a way of saying to ops "don't check permissions"
    if (!ps)
        return 0;
    // Check that the inode exists
    /*
    if (perms_requested == F_OK) {
	if (! inode->i_links_count ) return ENOENT;	// apparently deleted.
	if (inode->i_dtime) return ENOENT;		// apparently deleted.
	return 0;
    }
    */
    // allow root all access
    if (ctx->uid == 0)
        return 0;
    // check to see if we are the owner, in the file's group, or other
    if (inode->i_uid == ctx->uid)
    {   
        if (((perms_requested << 6) & perms) == (perms_requested << 6))
            return 0;
	syslog( LOG_DEBUG, "inode->i_uid = %d, ctx->uid = %d, perms_requested = %d, perms = %d", (int)inode->i_uid, (int)ctx->uid, perms_requested, perms );
        return EACCES;
    }
    // TODO: er, can't the user be in multiple groups? what's going on here?
    else if (inode->i_gid == ctx->gid)
    {   
        if (((perms_requested << 3) & perms) == (perms_requested << 3))
            return 0;
	syslog( LOG_DEBUG, "inode->i_gid = %d, ctx->gid = %d, perms_requested = %d, perms = %d", (int)inode->i_uid, (int)ctx->uid, (perms_requested * 7), perms );
        return EACCES;
    }
    else if ((perms_requested & perms) == perms_requested)
        return 0;
    //else if (!perms_requested) return 0;
    return EACCES;
}

int check_perms(perms_struct ps, ext2_ino_t ino, int perms_requested)
{
	if (!do_permissions_checks)
		return 0;
    static struct ext2_inode inode;
    // ps = NULL is a way of saying to ops "don't check permissions"
    if (!ps)
        return 0;
    if (read_inode(ino, &inode))
        return EIO;
    return check_perms_in_inode(ps, &inode, perms_requested);
}

int check_owner(perms_struct ps, struct ext2_inode *inode)
{
	if (!do_permissions_checks)
		return 0;
    // allow root all access
    if (((struct fuse_ctx *) ps)->uid == 0)
        return 0;
    // check to see if we are the owner
    if (((struct fuse_ctx *) ps)->uid == inode->i_uid)
		return 0;
	return EACCES;
}


int set_perms(perms_struct ps, struct ext2_inode *inode)
{
    const struct fuse_ctx *ctx = ps;
    if (!inode)
        return 1;
    inode->i_uid = ctx->uid;
    inode->i_gid = ctx->gid;
    return 0;
}


