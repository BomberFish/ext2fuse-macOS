#ifndef SYMLINK_H
#define SYMLINK_H

#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>

void op_readlink(fuse_req_t req, fuse_ino_t ino);
void op_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,                      const char *name);


#endif

