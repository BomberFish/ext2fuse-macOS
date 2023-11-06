#include "symlink.h"
#include "ext2fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <stddef.h>



// TODO: implement fast symlinks
//
void op_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,                      const char *name)
{
	int rc;
	unsigned int bytes;
	ext2_ino_t ino;
	ext2_file_t efile;
	struct ext2_inode inode;
	struct fuse_entry_param fep;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_symlink (link \"%s\", parent #%d, name \"%s\")",
		link, (int) parent, name);

	// makes a new file, and links it in. yes, use EXT2_FT_UNKNOWN
	// despite their being an EXT2_FT_SYMLINK, don't ask me.
	rc = do_create(ctx, EXT2FS_INO(parent), name, LINUX_S_IFLNK | 0777,
		    &ino, &inode, EXT2_FT_UNKNOWN);
	if (rc)
	{   
	fuse_reply_err(req, rc);
	return;
	}

	efile = do_open(NULL, ino, O_WRONLY);
	if (!efile)
	{
		fuse_reply_err(req, errno);
		return;
	}
	// add one to the string length, as we'd like to copy the '\0' too
	rc = do_write(efile, ino, link, strlen(link) + 1, (off_t) 0, &bytes);
	if (rc)
	{   
		fuse_reply_err(req, rc);
		return;
	}
	rc = do_file_close(efile);
	if (rc)
	{   
		fuse_reply_err(req, rc);
		return;
	}

	if (bytes != strlen(link) + 1)
	{   
		dbg("op_symlink: wrote only %d/%d bytes to file", bytes,
			strlen(link)+1);
		fuse_reply_err(req, EIO);
	}
	dbg("op_symlink: wrote %d/%d bytes to file", bytes, strlen(link) + 1);

	fep.ino = ino;
	fep.generation = inode.i_generation;
	fill_statbuf(ino, &inode, &fep.attr);
	fep.attr_timeout = 2.0;
	fep.entry_timeout = 2.0;
	fuse_reply_entry(req, &fep);
	return;
}


// The buffer allocated for the name is the length of the symlink inode, as
// the symlink name *is* the inode data :)
//
void op_readlink(fuse_req_t req, fuse_ino_t ino)
{
	int rc;
	char *buf;
	unsigned int bytes; 
	struct ext2_inode inode;
	const struct fuse_ctx *ctx = fuse_req_ctx(req);

	dbg("op_readlink(req, ino %d)", (int) ino);
	rc = read_inode(EXT2FS_INO(ino), &inode);
	if (rc)
	{   
		fuse_reply_err(req, EIO);
		return; 
	}   
	buf = (char *) malloc(inode.i_size+1);
	if (!buf) 
	{   
		fuse_reply_err(req, ENOMEM);
		return; 
	}   

	dbg("op_readlink: inode contents: i_mode=0%o, i_links_count=%d",
		inode.i_mode, inode.i_links_count);

	/* Try to handle fast symlinks */
	if (inode.i_blocks==0)
		strcpy(buf, (char*)inode.i_block);
	else
	{
		ext2_file_t efile = do_open(ctx, EXT2FS_INO(ino), O_RDONLY);
		if (!efile)
		{
			fuse_reply_err(req, errno);
			return;
		}   

		rc = do_read(efile, EXT2FS_INO(ino), inode.i_size,
			(off_t) 0, buf, &bytes);
		if (rc)
		{
			fuse_reply_err(req, rc);
			return;
		}
		rc = do_file_close(efile);
		if (rc)
		{
			fuse_reply_err(req, rc);
			return;
		}
		if (bytes != inode.i_size)
		{
			dbg ("op_readlink: do_read only read %d/%d bytes", bytes,inode.i_size);
			fuse_reply_err(req, EIO);
			return;
		}
	}
	fuse_reply_readlink(req, buf);
	free (buf);
}

