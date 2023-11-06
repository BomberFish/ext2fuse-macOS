#define FUSE_USE_VERSION 25
#include <fuse_lowlevel.h>
#include <syslog.h>
#include "ext2fs.h"

int check_perms_in_inode(perms_struct ps, struct ext2_inode *inode, int perms_requested);
int check_perms(perms_struct ps, ext2_ino_t ino, int perms_requested);
int check_owner(perms_struct ps, struct ext2_inode *inode);
int set_perms(perms_struct ps, struct ext2_inode *inode);