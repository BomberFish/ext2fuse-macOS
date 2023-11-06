#ifndef READDIR_H
#define READDIR_H

#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>

void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                             off_t off, struct fuse_file_info *fi);

#endif

