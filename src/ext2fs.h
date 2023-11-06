/*
 *  This program can be distributed under the terms of the GNU GPL v2.
 *  See the file COPYING.
 */

#ifndef __EXT2FS_H
#define __EXT2FS_H

#ifdef __APPLE__
#include <sys/types.h>
#endif
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <et/com_err.h>

#include <errno.h>

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include "truncate.h"

struct dirbuf {
	char *p;
	size_t size;
};

#ifndef __FUNCTION__ 
#define __FUNCTION__ __func__
#endif

#ifndef F_OK
#define F_OK 010
#endif

#ifndef R_OK 
#define R_OK 004
#endif

#ifndef W_OK
#define W_OK 002
#endif

#ifndef X_OK
#define X_OK 001
#endif

#ifdef DEBUG
#define dbg(...)\
	com_err("ext2fuse", 0, __VA_ARGS__)
#else
#define dbg(...)
#endif

#define min(x, y) ((x) < (y) ? (x) : (y))

#define EXT2FS_INO(ino) ((ext2_ino_t)(ino = (ino) < EXT2_ROOT_INO ? EXT2_ROOT_INO : (ino)))
#define EXT2FS_FILE(efile) ((void *) (unsigned long) (efile))

#define EXT2FUSE_VERSION "0.8.1"

/* this is declared in fileio.c of e2fsprogs, but we need it  */
struct ext2_file {
	errcode_t magic;
	ext2_filsys fs;
	ext2_ino_t ino;
	struct ext2_inode inode;
	int flags;
	__u64 pos;
	blk_t blockno;
	blk_t physblock;
	char *buf;
};

/* our filesystem! */
extern ext2_filsys fs;
				      
/* permissions struct, so we don't have to #include fuse stuff here */
typedef const void *perms_struct;
/* global bool to control whether permissions checking happens or not */
extern int do_permissions_checks;

/* perms.c, deals with fuse stuff */

int check_owner(perms_struct ps, struct ext2_inode *inode);

int check_perms(perms_struct, ext2_ino_t ino,
                int perms_requested);
int set_perms(perms_struct ps, struct ext2_inode *inode);

/* ext2fs.c */

void init_ext2_stuff();

int do_statvfs(struct statvfs *stbuf);
void fill_statbuf(ext2_ino_t, struct ext2_inode *, struct stat *);


int read_inode(ext2_ino_t, struct ext2_inode *);
int write_inode(ext2_ino_t, struct ext2_inode *);
int ext2_file_type(unsigned int);
int get_ino_by_name(ext2_ino_t ,const char *, ext2_ino_t *);

int do_lookup(perms_struct, ext2_ino_t,
			const char *, ext2_ino_t *, struct ext2_inode *);
int do_dir_iterate(ext2_ino_t, int,
		int (*)(struct ext2_dir_entry *, int, int, char *, void *), void *);

ext2_file_t do_open(perms_struct, ext2_ino_t, int);
int do_create(perms_struct perms, ext2_ino_t, const char *,
		mode_t,	ext2_ino_t *, struct ext2_inode *, int filetype_in_dir);

int do_link(ext2_ino_t, const char *, ext2_ino_t, int);
int do_unlink(ext2_ino_t, const char *, int);
int do_unlink_on_ino(ext2_ino_t, const char *, ext2_ino_t, int);

int do_read(struct ext2_file *, ext2_ino_t, size_t, off_t, char *,
			unsigned int *);
int do_write(struct ext2_file *, ext2_ino_t, const char *, size_t, off_t,
			unsigned int *);

int do_file_flush(struct ext2_file *fh);
int do_file_close(struct ext2_file *fh);

int do_mkdir(perms_struct, ext2_ino_t, const char *, mode_t,
		ext2_ino_t *);
int do_rmdir(perms_struct, ext2_ino_t, const char *);
int do_rmdir_on_ino(perms_struct perms, ext2_ino_t parent, const char *name,
		ext2_ino_t ino);

int do_rename_dir(ext2_ino_t parent, const char *name,
						ext2_ino_t newparent, const char *newname);

/* rename.c */
int do_rename(perms_struct perms, ext2_ino_t parent_ino,
		const char *name, ext2_ino_t newparent_ino, const char *newname);

/* mkdir.c */
errcode_t new_ext2fs_mkdir(perms_struct perms,
				ext2_ino_t parent, const char *name);

/* wipe_block.c */
int wipe_block(blk_t block);

#endif

