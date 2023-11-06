/*
 * mkdir.c --- make a directory in the filesystem
 * 
 * Copyright (C) 1994, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2fs.h"
#include "perms.h"

#ifndef EXT2_FT_DIR
#define EXT2_FT_DIR		2
#endif

// this has a consistent ordering of operations
//
errcode_t new_ext2fs_mkdir(perms_struct perms,
		ext2_ino_t parent, const char *name)
{
	errcode_t		retval;
	struct ext2_inode	parent_inode, inode;
	ext2_ino_t		ino;
	ext2_ino_t		scratch_ino;
	blk_t			blk;
	char			*block = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	// get a new inode number
	retval = ext2fs_new_inode(fs, parent, LINUX_S_IFDIR | 0755,
				  0, &ino);
	if (retval)
		goto cleanup;

	// get a data block number for the directory
	retval = ext2fs_new_block(fs, 0, 0, &blk);
	if (retval)
		goto cleanup;

	// create a scratch template for the directory
	retval = ext2fs_new_dir_block(fs, ino, parent, &block);
	if (retval)
		goto cleanup;

	// check parent exists
	retval = ext2fs_read_inode(fs, parent, &parent_inode);
	// we need write permission to the parent dir
	if (check_perms_in_inode(perms, &parent_inode, 02))
	{
		retval = EACCES;
		goto cleanup;
	}

	// create the inode structure....
	memset(&inode, 0, sizeof(struct ext2_inode));
	if (set_perms(perms, &inode))
	{
		retval = EIO;
		goto cleanup;
	}
	// TODO: test umasks; if user changes it, will it still work??
	inode.i_mode = LINUX_S_IFDIR | (0777 & ~fs->umask);
	inode.i_blocks = fs->blocksize / 512;
	inode.i_block[0] = blk;
	inode.i_links_count = 2;
	inode.i_ctime = inode.i_atime = inode.i_mtime =
		fs->now ? fs->now : time(NULL);
	inode.i_size = fs->blocksize;

	// allocate the inode and block so nobody else writes it
	ext2fs_block_alloc_stats(fs, blk, +1);
	ext2fs_inode_alloc_stats2(fs, ino, +1, 1);

	// write out the inode and inode data block
	retval = ext2fs_write_dir_block(fs, blk, block);
	if (retval)
		goto cleanup;
	retval = ext2fs_write_new_inode(fs, ino, &inode); 
	if (retval)
		goto cleanup;

	// link the directory into the filesystem hierarchy
	if (name) {
		retval = ext2fs_lookup(fs, parent, name, strlen(name), 0,
				       &scratch_ino);
		if (!retval) {
			retval = EXT2_ET_DIR_EXISTS;
			name = 0;
			goto cleanup;
		}
		if (retval != EXT2_ET_FILE_NOT_FOUND)
			goto cleanup;
		retval = ext2fs_link(fs, parent, name, ino, EXT2_FT_DIR);
		if (retval)
			goto cleanup;
	}

	// update parent inode's counts
	parent_inode.i_links_count++;
	retval = ext2fs_write_inode(fs, parent, &parent_inode);
	if (retval)
		goto cleanup;

cleanup:
	if (block)
		ext2fs_free_mem(&block);
	return retval;

}


