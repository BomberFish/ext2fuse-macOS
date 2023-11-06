#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <errno.h>

#include "ext2fs.h"


// Should have consistency now. Order is:
// 		increase links_count (either in inode, or in new parent if dir)
// 		link into new parent
//		delete any existing thing with our target name (changing link counts)
//		remove link in old parent (decreasing appropriate links_count)
//
// TODO: make sure rename() reverts changes on failure
//
// If it's a dir:
// 		the links_count for the old + new parent dirs need to change 
//
// If it's a file:
// 		the file links_count needs to change (to ensure consistency)
//
int do_rename(perms_struct perms, ext2_ino_t parent_ino,
		const char *name, ext2_ino_t newparent_ino, const char *newname)
{
	int ret;
	ext2_ino_t ino, target_ino=0;
	struct ext2_inode inode, parent_inode, newparent_inode;

	ret = get_ino_by_name(parent_ino, name, &ino);
	if (ret)
		return ENOENT;
    
	// We need to lookup the target_ino # before we create the new link,
	// as otherwise our lookup might fail.
	if (get_ino_by_name(newparent_ino, newname, &target_ino) == 0)
		dbg("Found previously existing target");

	// Increase link count to file (if it's a file)
	ret = read_inode(ino, &inode);
	if (ret)
		return EIO;
	if (!LINUX_S_ISDIR(inode.i_mode))
	{
		inode.i_links_count++;
		if ((ret = write_inode(ino, &inode)))
			return EIO;
	}
	else
	{
		// update newparent's inode count as it will contain the dir
		if ((ret = ext2fs_read_inode(fs, newparent_ino, &newparent_inode)))
			return ret;
		newparent_inode.i_links_count++;
		if ((ret = ext2fs_write_inode(fs, newparent_ino, &newparent_inode)))
			return ret;
	}

	// Make the new link (POSIX demands we always have a valid newname link)
	// After this call note we will have 2 same-name entries in the newparent!
	// Therefore do NOT do anything that relies on newname (eg del by name)
	ret = do_link(newparent_ino, newname,
	ino, ext2_file_type(inode.i_mode));
	if (ret)
		goto cleanup;
	dbg("new link to %s named %s has been made",
	name, newname);


	// If there is a file/dir to overwrite, we need to try and delete it now
	if (target_ino)
	{
		struct ext2_inode target_inode;
		// TODO: problem when dir -> empty dir (clobber)
		// Hmmm, seems to be fine in deleting the empty dir, the prob
		// comes in creating a new one :/

		if (read_inode(target_ino, &target_inode))
		{
			ret = EIO;
			goto cleanup;
		}

		if (LINUX_S_ISDIR(inode.i_mode))
		{
			if (!LINUX_S_ISDIR(target_inode.i_mode))
			{
				ret = ENOTDIR;
				goto cleanup;
			}

			// Deleting possibly empty directory...
			dbg("dir -> empty dir : clobber");
			// This call uses target_ino as the primary index, not newname.
			// This call also decreases the links_count in parent dir
			ret = do_rmdir_on_ino(perms, newparent_ino, newname, target_ino);
			if (ret)
			{
				if (ret == ENOTEMPTY)
					dbg("dir -> nonempty dir : aborting");
				goto cleanup;
			}
		}
		else
		{
			if (LINUX_S_ISDIR(target_inode.i_mode))
				return EISDIR;
			dbg("Unlinking file");
			// unlink (possibly delete) destination file
			ret = do_unlink_on_ino(newparent_ino, newname, target_ino, -1);
			if (ret)
				goto cleanup;
		}
	}

	// Now we want to remove the old link...
	if (!LINUX_S_ISDIR(inode.i_mode))
	{
		// remove original link to file which has now moved
		// -1 decreases link count by one
		ret = do_unlink(parent_ino, name, -1);
		if (ret)
			goto cleanup;
	}
	else
	{
		// First sort out the .. entry in the target directory inode
		// 1. add correct .. hard link
		// 1. remove old .. hard link
		ret = do_link(ino, "..", EXT2FS_INO(newparent_ino),
		EXT2_FT_DIR);
		if (ret)
			goto cleanup;
		if (ext2fs_unlink(fs, ino, "..", parent_ino, 0))
		{
			ret = EIO;
			goto cleanup;
		}
		//We don't actually want to call do_rmdir, as this
		//recursively removes all files etc in the dir.
		//do_unlink does various stuff we don't want, as well.
		//Really we just want to call ext2fs_unlink
		if (ext2fs_unlink(fs, parent_ino, name, 0, 0))
		{
			ret = EIO;
			goto cleanup;
		}
		// Decrease oldparents links count as it no longer contains the dir
		if ((ret = ext2fs_read_inode(fs, parent_ino, &parent_inode)))
			return ret;
		parent_inode.i_links_count--;
		if ((ret = ext2fs_write_inode(fs, parent_ino, &parent_inode)))
			return ret;

		dbg("removed old name \"%s\" from containing dir", name);
	}

	dbg("finished op_rename successfully");
	return 0;

cleanup:
	// Note value of ret needs to be preserved, so we know what the first error
	// was...

	// Need to remove the latest newname link (taking care to preserve any 
	// previously existing link with the same name)
	if (ext2fs_unlink(fs, newparent_ino, newname, ino, 0))
	{
		dbg("error unlinking newname in cleanup code!");
		return ret;
	}
	// Also must revert links_count entries
	if (!LINUX_S_ISDIR(inode.i_mode))
	{
		inode.i_links_count--;
		if ((ret = write_inode(ino, &inode)))
			return EIO;
	}
	else
	{
		// Decrease oldparents links count as it no longer contains the dir
		if (ext2fs_read_inode(fs, newparent_ino, &newparent_inode))
		{
			dbg ("error reading newparent inode in cleanup code!");
			return ret;
		}
		newparent_inode.i_links_count--;
		if (ext2fs_write_inode(fs, newparent_ino, &newparent_inode))
		{
			dbg ("error reading newparent inode in cleanup code!");
			return ret;
		}
	}
	return ret;
}


