#ifndef TRUNCATE_H
#define TRUNCATE_H

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <sys/types.h>

// saves inode to disk after setting new size
int set_file_size(ext2_ino_t ino,
		struct ext2_inode *inode, off_t desired_size);


// The main truncate routines -
// they support the creation of sparse gaps if you specify lengths
// larger than the size of the file
//
// do_truncate checks for write permissions on the file before
// opening, while ftruncate operates on an open file.
//
int do_ftruncate(struct ext2_file *fh, ext2_ino_t ino,
		off_t length);
int do_truncate(const void* perms, ext2_ino_t ino,
		off_t length);

#endif

