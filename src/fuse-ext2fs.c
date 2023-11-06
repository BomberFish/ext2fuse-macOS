/*
 *  Copyright (C) 2007-8, see the file AUTHORS for copyright owners.
 *
 *  This program can be distributed under the terms of the GNU GPL v2,
 *  or any later version. See the file COPYING.
 */

#define FUSE_USE_VERSION 25
#include <fuse_lowlevel.h>
#include <fuse_opt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// For basename
#include <libgen.h>

#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <stddef.h>

#include "ext2fs.h"

#include "symlink.h"
#include "readdir.h"

// For R_OK flags
#include <fcntl.h>
#include <getopt.h>

ext2_filsys fs;

/* fuse will do permission checks if we pass -o default_permissions
 * The permission checking code here probably needs removing
 */ 
int do_permissions_checks = 0;

struct options {
	char *device_name;
	char *mount_point;
	char *mount_options;
	int debug;
};
static struct options options;

static const char *EXEC_NAME = "ext2fuse";
static char def_opts[] = "fsname=";

// access: tells user whether it has the specified access rights to a file.
//
// R_OK, W_OK and X_OK request checking whether the file exists and has
// read, write and execute permissions, respectively. F_OK just requests
// checking for the existence of the file.
// note that R_OK, W_OK, X_OK are exactly the same as 0755 type permissions
//
void op_access(fuse_req_t req, fuse_ino_t ino, int mask)
{
	dbg("op_access (inode %d, mask 0%o)", (int) ino, mask);

	// check for RO filesystem
	if ((mask & W_OK) && !(fs->flags & EXT2_FLAG_RW))
	{
		fuse_reply_err(req, EROFS);
		return;
	}
	fuse_reply_err(req,
				check_perms(fuse_req_ctx(req), EXT2FS_INO(ino), mask));
}

// This is basically stat/fstat/lstat
// this needs no permission checks, as anyone who can search to this file,
// is allowed to stat it
//
void op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	int rc;
	struct stat stbuf;
	struct ext2_inode inode;

	dbg("op_getattr(req, ino %d, fuse_file_ info *)", (int) ino);

	memset(&stbuf, 0, sizeof(stbuf));
	rc = read_inode(EXT2FS_INO(ino), &inode);
	if (rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}
	fill_statbuf(ino, &inode, &stbuf);

	fuse_reply_attr(req, &stbuf, 1.0);
}

// op_setattr is used for chmod(), chown(), utime() and truncate() - Miklos
// TODO: test that permissions for setattr actually work!
// 
void op_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
				int to_set, struct fuse_file_info *fi)
{
	int rc;
	struct stat stbuf;
	struct ext2_inode inode;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_setattr(req, ino %d, stat*, to_set %d, fuse_file_info *)", (int) ino, to_set);
	memset(&stbuf, 0, sizeof(stbuf));

	// This must be before we read in the inode, as it may change things 
	// like # of blocks.
	if(to_set & FUSE_SET_ATTR_SIZE)
	{
		rc = do_truncate(ctx, EXT2FS_INO(ino), attr->st_size);
		if (rc)
		{
			fuse_reply_err(req, rc);
			return;
		}
	}

	rc = read_inode(EXT2FS_INO(ino), &inode);
	if(rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	if ((to_set & FUSE_SET_ATTR_MODE) && !check_owner(ctx, &inode))
		inode.i_mode = (inode.i_mode & ~0777) | attr->st_mode;

	if ((to_set & FUSE_SET_ATTR_UID) && !check_owner(ctx, &inode))
		inode.i_uid = attr->st_uid;

	if ((to_set & FUSE_SET_ATTR_GID) && !check_owner(ctx, &inode))
		inode.i_gid = attr->st_gid;

	if ((to_set & FUSE_SET_ATTR_ATIME) && !check_owner(ctx, &inode))
		inode.i_atime = attr->st_atime;

	if ((to_set & FUSE_SET_ATTR_MTIME) && !check_owner(ctx, &inode))
		inode.i_mtime = attr->st_mtime;
	else if (!check_owner(ctx, &inode))
		inode.i_mtime = time(NULL);

	rc = write_inode(EXT2FS_INO(ino), &inode);
	if(rc) {
		fuse_reply_err(req, EIO);
		return;
	}

	fill_statbuf(ino, &inode, &stbuf);
	fuse_reply_attr(req, &stbuf, 1.0);
}

void op_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param fe;
	struct ext2_inode inode;
	ext2_ino_t ino;
	int rc;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_lookup(req, parent ino %d, name %s)", (int) parent, name);

	if(!strncmp(name, "(null)", 6)) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	rc = do_lookup(ctx, EXT2FS_INO(parent), name, &ino, &inode);
	if(rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	fe.ino = ino;
	fe.generation = inode.i_generation;
	fill_statbuf(ino, &inode, &fe.attr);
	fe.attr_timeout = 2.0;
	fe.entry_timeout = 2.0;
	fuse_reply_entry(req, &fe);
}

void op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
	dbg("op_open(req, ino %d, flags 0%o)", (int) ino, fi->flags);
	ext2_file_t efile = do_open(ctx, EXT2FS_INO(ino), fi->flags);

	if(!efile)
		fuse_reply_err(req, errno);
	else {
		fi->fh = (unsigned long) efile;
		fuse_reply_open(req, fi);
	}
}

void op_create(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode, struct fuse_file_info *fi)
{
	int rc;
	ext2_ino_t ino;
	ext2_file_t efile;
	struct ext2_inode inode;
	struct fuse_entry_param fep;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_create(req, parent #%d, name %s, mode 0%o,"\
		" fuse_file_info {fi->flags 0%o,...})",
		(int) parent, name, mode, fi->flags);

	// write the inode
	rc = do_create(ctx, EXT2FS_INO(parent), name,
			LINUX_S_IFREG | (mode & 0777), &ino, &inode, EXT2_FT_REG_FILE);
					
	if(rc)
		goto err_out;

	// open the file, don't check for perms as we already did in do_create,
	// and we don't ever want create working without open working
	efile = do_open(NULL, ino, fi->flags & ~O_CREAT);
	if(!efile) {
		rc = errno;
		goto err_out;
	}

	fi->fh = (unsigned long) efile;
	fep.ino = ino;
	fep.generation = inode.i_generation;
	fill_statbuf(ino, &inode, &fep.attr);
	fep.attr_timeout = 2.0;
	fep.entry_timeout = 2.0;
	fuse_reply_create(req, &fep, fi);
	return;

err_out:
	fuse_reply_err(req, rc);
}

void op_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	dbg("op_unlink(req, parent ino %d, name %s)", (int) parent, name);
	// we only need write access on the parent to unlink
	int ret = check_perms(fuse_req_ctx(req), EXT2FS_INO(parent), W_OK);
	if (ret)
	{
		fuse_reply_err(req, ret);
		return;
	}

	ret = do_unlink(EXT2FS_INO(parent), name, -1);
	fuse_reply_err(req, ret);
}

void op_rename(fuse_req_t req, fuse_ino_t parent_ino, const char *name,
			fuse_ino_t newparent_ino, const char *newname)
{
	int ret;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_rename(oldparent %d, \"%s\" -> newparent %d, \"%s\")",
		(int) parent_ino, name, (int) newparent_ino, newname);

	// We need write permission to BOTH directories
	ret = check_perms(ctx, EXT2FS_INO(parent_ino), W_OK);
	ret |= check_perms(ctx, EXT2FS_INO(newparent_ino), W_OK);
	if (ret)
	{
		fuse_reply_err(req, ret);
		return;
	}

	ret = do_rename(ctx, parent_ino, name, newparent_ino, newname);
	fuse_reply_err(req, ret);
}

// order of ops, for consistency:
// 		increase link count
// 		do link
//
void op_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
				const char *newname)
{
	int ret;
	struct ext2_inode inode;
	struct fuse_entry_param fe;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_link(req, ino %d, new parent ino %d, newname %s)", (int) ino, (int) newparent,  newname);
	// we don't need any perms to make a link to something,
	// only the std parent write perms
	ret = check_perms(ctx, EXT2FS_INO(newparent), W_OK);
	if (ret)
	{
		fuse_reply_err(req, ret);
		return;
	}

	// read inode of file to link to
	ret = read_inode(EXT2FS_INO(ino), &inode);
	if (ret)
	{
		fuse_reply_err(req, EIO);
		return;
	}
	// increase link count, and save new value
	inode.i_links_count++;
	ret = write_inode(EXT2FS_INO(ino), &inode);
	if (ret)
	{
		fuse_reply_err(req, EIO);
		return;
	}
	ret = do_link(EXT2FS_INO(newparent), newname, EXT2FS_INO(ino),
		ext2_file_type(inode.i_mode));
	if(ret)
	{
		// if linking fails, revert the link count
		inode.i_links_count--;
		ret = write_inode(EXT2FS_INO(ino), &inode);
		if (ret)
		{
			fuse_reply_err(req, EIO);
			return;
		}
		fuse_reply_err(req, ENOENT);
		return;
	}

	fe.ino = ino;
	fe.generation = inode.i_generation;
	fill_statbuf(ino, &inode, &fe.attr);
	fe.attr_timeout = 2.0;
	fe.entry_timeout = 2.0;

	fuse_reply_entry(req, &fe);
}

void op_statfs(fuse_req_t req)
{
	struct statvfs stbuf;

	dbg("op_statfs(req)");
	do_statvfs(&stbuf);
	fuse_reply_statfs(req, &stbuf);
}


void op_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			off_t off, struct fuse_file_info *fi)
{
	int rc;
	void *buf = malloc(size);
	unsigned int bytes;

	dbg("op_read(req, ino %d, size %d, off %d, file_info)", (int) ino, (int) size,  (int)off);
	rc = do_read(EXT2FS_FILE(fi->fh), EXT2FS_INO(ino),
		size, off, buf, &bytes);

	if(rc)
		fuse_reply_err(req, rc);
	else
		fuse_reply_buf(req, buf, bytes);

	free(buf);
}

void op_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
			size_t size, off_t off, struct fuse_file_info *fi)
{
	int rc;
	unsigned int bytes;

	dbg("op_write(req, ino %d, buf %s, size %d, off %d, file_info)", (int) ino, buf, (int) size,  (int)off);
	rc = do_write(EXT2FS_FILE(fi->fh), ino, buf, size, off, &bytes);
	if(rc)
		fuse_reply_err(req, rc);
	else
		fuse_reply_write(req, bytes);
}

void op_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	int rc;
	dbg("op_flush(req, ino %d, file_info)", (int) ino);
	rc = do_file_flush(EXT2FS_FILE(fi->fh));
	fuse_reply_err(req, rc);
}

void op_release(fuse_req_t req, fuse_ino_t ino,
			struct fuse_file_info *fi)
{
	int rc;
	dbg("op_release(req, ino %d, file_info)", (int) ino);
	rc = do_file_close(EXT2FS_FILE(fi->fh));
	if (rc)
		fuse_reply_err(req, EIO);
	else
		fuse_reply_err(req, 0);
}

// from the fuse header:
// 	If the datasync parameter is non-zero, then only the user data
// 	should be flushed, not the meta data.
// we only got user data though...
void op_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
			struct fuse_file_info *fi)
{
	// TODO: test fsync somehow!
	dbg("op_fsync(req, ino %d, data sync %d, file_info)", (int) ino, datasync);
	ext2fs_flush(fs);
	fuse_reply_err(req, 0);
}

void op_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
			mode_t mode)
{
	struct fuse_entry_param fe;
	ext2_ino_t ino;
	struct ext2_inode inode;
	int ret;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	if(parent == 1) parent = EXT2_ROOT_INO;

	dbg("op_mkdir(req, parent ino %d, name %s, mode 0%o)", (int) parent, name, mode);
	ret = do_mkdir(ctx, EXT2FS_INO(parent), name, mode, &ino);
	if(ret) {
		fuse_reply_err(req, ret);
		return;
	}

	ret = read_inode(ino, &inode);
	if(ret) {
		fuse_reply_err(req, EIO);
		return;
	}

	fe.ino = ino;
	fe.generation = inode.i_generation;
	fill_statbuf(ino, &inode, &fe.attr);
	fe.attr_timeout = 2.0;
	fe.entry_timeout = 2.0;
	fuse_reply_entry(req, &fe);
}

void op_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	int ret;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_rmdir(req, parent ino %d, name %s)", (int) parent, name);
	// need to be able to write to the parent dir
	ret = check_perms(ctx, EXT2FS_INO(parent), W_OK);
	if (!ret)
		ret = do_rmdir(ctx, EXT2FS_INO(parent), name);
	fuse_reply_err(req, ret);
}

// The userdata should be a char * specifying the fs device to be mounted
void op_init(void *userdata)
{
	char *fs_device_name = (char *) userdata;
	errcode_t ret;

	dbg("op_init(device_name %s)", fs_device_name);
	// ext2fs_open(const char *name, int flags,
	// 					int superblock, unsigned int block_size,
	// 					io_manager manager, ext2_filsys *ret_fs);
	// superblock is a chosen block # for the superblock.
	// if superblock == 0, the default position of 1024 bytes in is assumed.
	// if superblock != 0, must spec block_size.
	ret = ext2fs_open(fs_device_name,
				EXT2_FLAG_RW | EXT2_FLAG_JOURNAL_DEV_OK,
				0, 0, unix_io_manager, &fs);
				
	if (ret)
	{
		com_err("fuse-ext2fs", ret, "while trying to open %s",
			fs_device_name);
		return;
	}

	ret = ext2fs_read_inode_bitmap(fs);
	if (ret)
	{
		com_err("fuse-ext2", ret, "while reading inode bitmap");
		return;
	}

	ret = ext2fs_read_block_bitmap(fs);
	if (ret)
	{
		com_err("fuse-ext2", ret, "while reading block bitmap");
		return;
	}

	printf("fuse-ext2 initialized for device: %s\n", fs->device_name);
	printf("block size is %d\n", fs->blocksize);
}

void op_destroy(void *userdata)
{
	errcode_t ret;

	dbg("op_destroy()");
	ret = ext2fs_close(fs);
	if (ret)
	{
		com_err("fuse-ext2fs", ret, "while trying to close device %s",
			fs->device_name);
	}
	printf("fuse-ext2fs destroyed\n");
}

static struct fuse_lowlevel_ops ext2fs_ops = {
	.init           = op_init,
	.destroy        = op_destroy,
	.lookup         = op_lookup,
	.forget         = NULL,
	.getattr        = op_getattr,
	.setattr        = op_setattr,
	.readlink       = op_readlink,
	.mknod		= NULL,
	.mkdir          = op_mkdir,
	.rmdir          = op_rmdir,
	.symlink        = op_symlink,
	.rename         = op_rename,
	.link           = op_link,
	.unlink         = op_unlink,
	.open           = op_open,
	.read           = op_read,
	.write          = op_write,
	.flush          = op_flush,
	.release        = op_release,
	.fsync          = op_fsync,
	.opendir        = op_open,
	.readdir        = op_readdir,
	.releasedir     = op_release,
	.fsyncdir       = NULL,
	.statfs         = op_statfs,
	.setxattr       = NULL,
	.getxattr       = NULL,
	.listxattr      = NULL,
	.removexattr    = NULL,
	.access         = op_access,
	.create         = op_create,
};

int opt_proc(void *data, const char *arg, int key,
                               struct fuse_args *outargs)
{
	printf("arg=%s, int key=%d\n", arg, key); 
	return 0;
}

void version(const char *prog_name)
{
	printf("%s version %s\n",prog_name,EXT2FUSE_VERSION);
}

static int strappend(char **dest, const char *append)
{
	char *p;
	size_t size;
	
	if (!dest)
		return -1;
	if (!append)
		return 0;
	
	size = strlen(append) + 1;
	if (*dest)
		size += strlen(*dest);
	
	p = realloc(*dest, size);
    	if (!p) {
		dbg("Memory realloction failed");
		return -1;
	}
	
	if (*dest)
		strcat(p, append);
	else
		strcpy(p, append);
	*dest = p;
	
	return 0;
}

void usage(const char *prog_name)
{
	printf(	"%s devicename mountpoint [--options fuse-option1,fuse-option2,...]\n",
			prog_name);
	printf(	"%s --help\n", prog_name);
	printf(	"%s --version\n", prog_name);
	printf(	"\nSee your distribution's FUSE documentation for FUSE mount options.\n");
}

/**
 * parse_options - Read and validate the programs command line
 * Read the command line, verify the syntax and parse the options.
 *
 * Return:   0 success, -1 error.
 */
static int parse_options(int argc, char *argv[])
{
	int c;

	static const char *sopt = "-o:hv";
	static const struct option lopt[] = {
		{ "options",				required_argument,	NULL, 'o' },
		{ "help",					no_argument,		NULL, 'h' },
		{ "version",				no_argument,		NULL, 'v' },
		{ NULL,		 0,			NULL,  0  }
	};

	opterr = 0; /* We'll handle the errors, thank you. */

	while ((c = getopt_long(argc, argv, sopt, lopt, NULL)) != -1) {
		switch (c) {
		case 1:	/* A non-option argument */
			if (!options.device_name) {
				options.device_name=optarg;
			} else if (!options.mount_point) {
				options.mount_point = optarg;
			} else {
				dbg("You must specify exactly one "
					"device and exactly one mount "
					"point");
				return -1;
			}
			break;
		case 'o':
			if (options.mount_options)
				if (strappend(&options.mount_options, ","))
					return -1;
			if (strappend(&options.mount_options, optarg))
				return -1;
			break;
		case 'h':
			usage(EXEC_NAME);
			exit(9);
		case 'v':
			version(EXEC_NAME);
			exit(9);
		default:
			dbg("Unknown option '%s'",
				argv[optind - 1]);
			return -1;
		}
	}

	if (!options.device_name) {
		dbg("No device is specified.");
		return -1;
	}
	if (!options.mount_point) {
		dbg("No mountpoint is specified.");
		return -1;
	}
	/* Add the default mount options */
	if (options.mount_options)
		if (strappend(&options.mount_options, ","))
			return -1;
	if (strappend(&options.mount_options,def_opts) || 
		strappend(&options.mount_options,options.device_name))
		return -1;

	return 0;
}

static int try_fuse_mount(char *mount_options)
{
	int fc = -1;
	struct fuse_args margs = FUSE_ARGS_INIT(0, NULL);
	
	/* The fuse_mount() options get modified, so we always rebuild it */
	if ((fuse_opt_add_arg(&margs, EXEC_NAME) == -1 ||
	     fuse_opt_add_arg(&margs, "-o") == -1 ||
	     fuse_opt_add_arg(&margs, mount_options) == -1)) {
		dbg("Failed to add FUSE mount options.");
		goto free_args;
	}
	
	fc = fuse_mount(options.mount_point, &margs);

free_args:
	fuse_opt_free_args(&margs);
	return fc;
		
}

int main(int argc, char *argv[])
{
	struct fuse_args custom_args = FUSE_ARGS_INIT(0,NULL);
	int err = -1;
	int fd;

	init_ext2_stuff();
	
	if(!parse_options(argc,argv))
	{
		printf("%s is to be mounted at %s\n",
			options.device_name, options.mount_point);
	} else {
		usage(EXEC_NAME);
		return err;
	}

	if ((fd = try_fuse_mount(options.mount_options))!=-1) {
		struct fuse_session *se=(struct fuse_session*)1;
		if (fuse_opt_add_arg(&custom_args, "") == -1)
			se = NULL;

		// Create the low-level session, and set the userdata to be
		// passed to op_init as the device name
		if(se!=NULL)
			se = fuse_lowlevel_new(&custom_args, &ext2fs_ops,
				sizeof(ext2fs_ops), options.device_name);
		if (se != NULL) {
			if (fuse_set_signal_handlers(se) != -1) {
				struct fuse_chan *ch = fuse_kern_chan_new(fd);
				if (ch != NULL) {
					fuse_session_add_chan(se, ch);
					err = fuse_session_loop(se);
				}
				fuse_remove_signal_handlers(se);
			}
			fuse_session_destroy(se);
		}
		close(fd);
	}
	fuse_unmount(options.mount_point);
	fuse_opt_free_args(&custom_args);

	return err ? 1 : 0;
}

