bin_PROGRAMS = ext2fuse
ext2fuse_SOURCES = ext2fs.c mkdir.c readdir.c symlink.c wipe_block.c fuse-ext2fs.c perms.c rename.c truncate.c ext2fs.h readdir.h symlink.h truncate.h wipe_block.h
ext2fuse_CFLAGS = -I/usr/include/fuse -I/usr/local/include/fuse -I../lib -I../lib/et -I../lib/ext2fs -D_FILE_OFFSET_BITS=64 
ext2fuse_LDADD = ../lib/et/libcom_err.a ../lib/ext2fs/libext2fs.a

