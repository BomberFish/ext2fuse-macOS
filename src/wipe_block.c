#include "ext2fs.h"
#include <ext2fs/ext2_io.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "wipe_block.h"

// TODO: implement a large random pool buffer here to speed up block wipes
static const char *get_random_block(unsigned int blocksize)
{
	static char *buffer = NULL;
	static unsigned int buffer_size = 0;
	if (!buffer)
	{
		if (buffer_size)
		{
			dbg("strange error, buffer_size != 0");
			exit(1);
		}
		// This should only happen once, on the first call.
		// note this data is persistant, but it's only one block
		buffer = (char *) malloc((size_t) blocksize);
		if (!buffer)
			return (buffer = NULL);
		buffer_size = blocksize;
	}
	else if (buffer_size != blocksize)
	{
		dbg("fatal error, blocksize has changed! dying...");
		exit(1);
	}

	int fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		return NULL;
	
	if (read(fd, buffer, (size_t) blocksize) != (ssize_t) blocksize)
	{
		close(fd);
		dbg("read from dev/urandom failed!");
		return NULL;
	}

	close(fd);
	return buffer;
}


// "borrowed" from unix_io.c, in libext2fs
//
struct unix_cache {
    char        *buf;
    unsigned long   block;
    int     access_time;
    unsigned    dirty:1;
    unsigned    in_use:1;
};

#define CACHE_SIZE 8
struct unix_private_data {
    int magic;
    int dev;
    int flags;
    int access_time;
    ext2_loff_t offset;
    struct unix_cache cache[CACHE_SIZE];
};



static int test_wipe_block(blk_t block)
{
	int written;
	io_channel channel = fs->io;
	struct unix_private_data *data = (struct unix_private_data *)
			channel->private_data;	
	
	ext2_loff_t location = ((ext2_loff_t) block * fs->blocksize)
							+ data->offset;

	char *buffer = malloc((size_t) fs->blocksize);
	if (!buffer)
	{
		dbg("no memory left!");
		return 1;
	}
	memset(buffer, 't', fs->blocksize);
	
	if (lseek(data->dev, location, SEEK_SET) == -1)
	{
		dbg("seek failed!");
		return errno;
	}

	dbg("write(dev, buffer, blocksize %d)", fs->blocksize);
	written = write(data->dev, buffer, fs->blocksize);
	if (written != fs->blocksize)
	{
		dbg("write failed with ret %d", written);
		return 1;
	}
	else
		dbg("succesfully test_wiped block %d", (int) block);
	return 0;

}

// Non-cached direct IO here, for simplicity.

static int secure_wipe_block(blk_t block)
{
	// slightly hacky interface that avoids the cache but is simple to code
	// errcode_t rc = (unix_io_manager->write_blk) (fs.io, block, 1, buffer);

	int written;
	io_channel channel = fs->io;
	struct unix_private_data *data = (struct unix_private_data *)
			channel->private_data;	
	
	ext2_loff_t location = ((ext2_loff_t) block * fs->blocksize)
							+ data->offset;

	const char *buffer = get_random_block(fs->blocksize);
	if (!buffer)
	{
		dbg("failed to get_random_block");
		return 1;
	}

	if (lseek(data->dev, location, SEEK_SET) == -1)
	{
		dbg("seek failed!");
		return errno;
	}

	dbg("write(dev, buffer, blocksize %d)", fs->blocksize);
	written = write(data->dev, buffer, fs->blocksize);
	if (written != fs->blocksize)
	{
		dbg("write failed with ret %d", written);
		return 1;
	}
	else
		dbg("succesfully test_wiped block %d", (int) block);
	return 0;
}

static int null_wipe_block(blk_t block)
{
	dbg("null_wiped block %d", (int) block);
	return 0;
}


// For std ext2 file system, don't wipe blocks after use
//wipe_block_proc wipe_block_procedure = null_wipe_block;
wipe_block_proc wipe_block_procedure = test_wipe_block;

