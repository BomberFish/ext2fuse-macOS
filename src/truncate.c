#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/statvfs.h>
#include <time.h>
#include <string.h>
#include <stddef.h>

#include "ext2fs.h"

#include "wipe_block.h"

#ifdef DEBUG
#define ext2_err(rc, fmt, arg...) \
    do { \
        com_err(__FUNCTION__, rc, fmt,  ## arg); \
    } while(0)
#else
    #define ext2_err(rc, fmt, arg...) \
        do { } while(0)
#endif



int set_file_size(ext2_ino_t ino,
        struct ext2_inode *inode, off_t desired_size)
{
    // Set the new file size:
    // rc = ext2fs_file_set_size(fh, inode.i_size + *bytes);
    // I've inlined this lib function.
    // Mainly because the original implementation was unfinished,
    // but also inefficient.
    // It couldn't deal with truncations :(
    //
    // (Grepping for the func name should *only* show up here, and mind
    //  not to delete any part of this comment as a test case relies on it)

	inode->i_mtime = time(NULL);
    inode->i_size = desired_size;
    dbg ("new inode.i_size = %d", (int) (inode->i_size));
    // this line was in the original func, so I'm keeping it...
    //i_size_high is defined to be i_dir_acl, probably something for
    // uberbig files only
    inode->i_size_high = 0;

    return ext2fs_write_inode(fs, ino, inode);
}

// for info.delete_indirect_flags
#define DEL_SINGLE_INDIRECT 1
#define DEL_ALL_BUT_TRIPLE  2
#define DEL_ALL_INDIRECT    4

struct truncate_blocks_info
{
    int last_block_to_keep;
    int num_in_indirect_block;
    int delete_indirect_flags;
    int total_num_blocks;
    // So we can trigger the zeroing of the indirect ptrs in the inode.
    // the direct block ptrs are 0'ed from a more efficient memset.
    // Denotes which of the fields (indirect, d_ind, t_ind) to clear.
    // if =1, clears all 3, =2, clears d_ind and t_ind, =3, clear t_ind.
    int clear_indirect_ptrs_from_inode;
};

static int truncate_blocks_proc(ext2_filsys fs, blk_t *blocknr,
                int blockcnt, void *private)
{
    struct truncate_blocks_info *infp =
            (struct truncate_blocks_info *) private;
    blk_t block = *blocknr;

    dbg("shorten: truncate_blocks_proc(block %d)", block);
    dbg("shorten: truncate_blocks_proc: current_block = %d", blockcnt);
    dbg("shorten: truncate_blocks_proc: delete_indirect_flags = %d",
        infp->delete_indirect_flags);

    // if called on a indirect block...
    if (blockcnt < 0)
    {
        switch (blockcnt) {
            case BLOCK_COUNT_IND:
                dbg("encountered indirect block!");
                if (infp->delete_indirect_flags)
                {
                    dbg("wiping");
                    ext2fs_block_alloc_stats(fs, block, -1);
                    *blocknr = 0;
					if ((wipe_block_procedure)(block))
						exit(1);
                    return BLOCK_CHANGED;
                }
                break;
            case BLOCK_COUNT_DIND:
                dbg("encountered double-indirect block");
                if (infp->delete_indirect_flags &
                        (DEL_ALL_BUT_TRIPLE | DEL_ALL_INDIRECT))
                {
                    dbg("wiping");
                    ext2fs_block_alloc_stats(fs, block, -1);
                    *blocknr = 0;
					if ((wipe_block_procedure)(block))
						exit(1);
                    return BLOCK_CHANGED;
                }
                break;
            case BLOCK_COUNT_TIND:
                dbg("encountered triple-indirect block");
                if (infp->delete_indirect_flags & DEL_ALL_INDIRECT)
                {
                    dbg("wiping");
                    ext2fs_block_alloc_stats(fs, block, -1);
                    *blocknr = 0;
					if ((wipe_block_procedure)(block))
						exit(1);
                    return BLOCK_CHANGED;
                }
                break;
            default:
                break;
        }
        infp->total_num_blocks++;
        return 0;
    }
    // we have now dealt with all indirect blocks, blockcnt is guarranteed
    // to be > 0

    // if we don't want this block, remove it, and any indirect blocks that
    // are no longer needed.
    if (blockcnt > infp->last_block_to_keep)
    {
        dbg("wiping!");
        ext2fs_block_alloc_stats(fs, block, -1);
        *blocknr = 0;
		if ((wipe_block_procedure)(block))
			exit(1);

        // Work out whether this is the 0th block of an indirect block, in
        // which case we can remove some(!) indirect blocks.
        //
        // This was a pain to work out, use a diagram!
        // and remember that the iteration is in order, from top
        // to bottom, so there are a few effeciencies built in :/
        if (blockcnt < 12)
            return BLOCK_CHANGED;
        else if (blockcnt == 12)
        {
            infp->clear_indirect_ptrs_from_inode = 1;
            infp->delete_indirect_flags |= DEL_ALL_INDIRECT;
            return BLOCK_CHANGED;
        }
        else if (blockcnt == infp->num_in_indirect_block + EXT2_NDIR_BLOCKS)
        {
            infp->clear_indirect_ptrs_from_inode = 2;
            infp->delete_indirect_flags |= DEL_ALL_INDIRECT;
            return BLOCK_CHANGED;
        }
        else if (blockcnt > infp->num_in_indirect_block + EXT2_NDIR_BLOCKS)
        {
            int i = blockcnt - EXT2_NDIR_BLOCKS;
            // if i is a multiple of 1024...
            // note no return here, as a more important flag may yet be set.
            if ((i % (infp->num_in_indirect_block)) == 0)
                infp->delete_indirect_flags |= DEL_SINGLE_INDIRECT;

            i -= infp->num_in_indirect_block +
                (infp->num_in_indirect_block * infp->num_in_indirect_block);
            if (i == 0)
            {
                infp->clear_indirect_ptrs_from_inode = 3;
                infp->delete_indirect_flags |= DEL_ALL_INDIRECT;
                return BLOCK_CHANGED;
            }
            else if (i > 0)
            {
                // if i is a multiple of (1024^2)
                if ((i % (infp->num_in_indirect_block *
                        infp->num_in_indirect_block)) == 0)
                    infp->delete_indirect_flags |= DEL_ALL_BUT_TRIPLE;
                return BLOCK_CHANGED;
            }
        }
        return BLOCK_CHANGED;
    }
    dbg("keeping!");
    infp->total_num_blocks++;
    return 0;
}


// do_shorten only shortens files
// does not alter seek.
//
// consistency:
//      save new i_size, as that's what read() counts on
//      iterate through blocks, changing i_block[]
//      re-read inode to get new block struct (guh)
//      save # blocks info to read inode
//      write inode
//
//  This is quite inefficient, 3 inode writes,
//  given we already have an open file. To improve that, I'd have to write
//  my own block iterator
//
static int do_shorten(struct ext2_file *fh,
                ext2_ino_t ino, off_t length)
{
    int rc;
    struct truncate_blocks_info info;
    struct ext2_inode inode;

    dbg("do_shorten(fs, fh, ino %d, length %ld)", (int) ino, (long) length);
    // check the length arg will actually shorten the file
    if (length == fh->inode.i_size)
        return 0;
    else if (length > fh->inode.i_size)
        return EFAULT;

    // Remember, start counting from 0!
    // division always rounds down
	info.last_block_to_keep = length ? ((length - 1)/fs->blocksize) : -1;
    info.num_in_indirect_block = (fs->blocksize)/sizeof(__u32);
    info.delete_indirect_flags = 0;
    info.clear_indirect_ptrs_from_inode = 0;
    info.total_num_blocks = 0;
    dbg("blocksize = %d", fs->blocksize);
    dbg("last_block_to_keep = %d", info.last_block_to_keep);
    dbg("num_in_indirect_block = %d", info.num_in_indirect_block);


    // set file size
    rc = set_file_size(ino, &fh->inode, length);
    if (rc)
        return rc;

    //  call the block iterator to go through blocks in order.
    //  this also removes indirect blocks, if necessary.
    //  THIS CHANGES THE INODE!
    ext2fs_block_iterate(fs, ino, BLOCK_FLAG_DEPTH_TRAVERSE,
        NULL, truncate_blocks_proc, &info);

    // re-read inode values
    rc = ext2fs_read_inode(fs, ino, &inode);
    if (rc)
        return rc;

    // Set the number of (block/sectors) the file is using...
    // Note that i_blocks counts from 1, not 0
    // i_blocks isn't actually the number of blocks, that'd be too easy.
    // Instead, it counts the number of 512 byte sectors, like the kernel.
    // It also includes any indirect blocks.
    // TODO: apparently sector size is not always 512, e.g. on CDROMs...
    inode.i_blocks = (info.total_num_blocks * fs->blocksize) / 512;
    dbg("number of (4096 sized) blocks used = %d", info.last_block_to_keep + 1);
    dbg("total num (4096) blocks, incl. indirect  = %d", info.total_num_blocks);
    dbg("i_blocks  = %d", inode.i_blocks);

    ext2fs_mark_bb_dirty(fs);
    return ext2fs_write_inode(fs, ino, &inode);
}

// do_lengthen emulates sparse gaps in files
//      seek (to end)
//      allocate gap buffer
//      write gap
//      free gap buffer
//      unseek
//      set new file size in copy of inode
//      write inode
//
// does *not* change the seek position!
// TODO: actually implement sparse file support, as opposed to emulating it
// This is done by setting block ptrs to 0.
// will have to hack the read end as well.
//
static int do_lengthen(struct ext2_file *fh, ext2_ino_t ino, off_t length)
{
    __u64 start_pos = fh->pos;
    unsigned int bytes_written;
    size_t gap_size = length - fh->inode.i_size;
    char *gap;
    int rc;

    // check the length arg will actually lengthen the file
    if (length == fh->inode.i_size)
        return 0;
    else if (length < fh->inode.i_size)
        return EFAULT;

    // seek to the end
    rc = ext2fs_file_llseek(fh, 0, SEEK_END, NULL);
    if(rc) {
        ext2_err(rc, "while seeking to end of %d", ino);
        return rc;
    }
    // allocate the buffer
    gap = (char *) calloc(gap_size, sizeof(char));
    if (!gap)
    {
        ext2fs_file_llseek(fh, start_pos, SEEK_SET, NULL);
        return ENOMEM;
    }
    // write the gap
    dbg("ext2fs_file_write(fh, buf, size %d, bytes)", (int) gap_size);
    rc = ext2fs_file_write(fh, gap, gap_size, &bytes_written);
    // free buffer
    free(gap);
    if (rc) {
        ext2_err(rc, "while writing file %d", ino);
        ext2fs_file_llseek(fh, start_pos, SEEK_SET, NULL);
        return rc;
    }
    // unseek
    rc = ext2fs_file_llseek(fh, start_pos, SEEK_SET, NULL);
    if (rc) return rc;
    // double check num bytes written
    if ((size_t) (bytes_written) != gap_size)
    {
        dbg("ext2fs_file_write only wrote %d/%d bytes", bytes_written,
            (int) gap_size);
        return EIO;
    }

    rc = set_file_size(ino, &fh->inode, length);
    if (rc)
        dbg("set_file_size reported error");
    return rc;
}



int do_ftruncate(struct ext2_file *fh, ext2_ino_t ino,
            off_t length)
{
    dbg("do_ftruncate(fs, fh, ino %d, length %ld)", (int) ino, (long) length);

    if (length > fh->inode.i_size)
        return do_lengthen(fh, ino, length);
    else
        return do_shorten(fh, ino, length);
}


int do_truncate(perms_struct perms, ext2_ino_t ino,
                off_t length)
{
    int rc;
    struct ext2_file *fh = do_open(perms, ino, O_CREAT | O_WRONLY);
    if (!fh)
        return errno;

    rc = do_ftruncate(fh, ino, length);
    if (rc)
        do_file_close(fh);
    else
        rc = do_file_close(fh);
    return rc;
}

