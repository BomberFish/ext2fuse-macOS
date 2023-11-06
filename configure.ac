#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([ext2fuse], [0.8.1], [ext2fuse-user@lists.sourceforge.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([lib/ext2fs/rs_bitmap.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PATH_PROG(CHMOD, chmod, :)

# Checks for libraries.
AC_CHECK_LIB([fuse], [fuse_mount])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_MAJOR
AC_CHECK_HEADERS([fcntl.h malloc.h mntent.h netinet/in.h paths.h stddef.h stdlib.h string.h linux/fd.h sys/file.h sys/ioctl.h sys/mount.h sys/param.h sys/statvfs.h sys/time.h sys/types.h sys/stat.h sys/mkdev.h sys/ioctl.h sys/resource.h sys/mman.h sys/prctl.h sys/disklabel.h sys/queue.h errno.h unistd.h utime.h])
AC_CHECK_HEADERS(sys/disk.h sys/mount.h,,,
[[
#if HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#endif
]])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_VPRINTF
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_GETMNTENT
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_STAT
AC_FUNC_UTIME_NULL
AC_CHECK_FUNCS([ftruncate getmntent getmntinfo getpagesize hasmntopt memmove memset munmap select strchr strdup strerror strrchr strtol strtoul strtoull uname utime])

AC_CONFIG_FILES([
	Makefile
	lib/et/Makefile
	lib/ext2fs/Makefile
	src/Makefile])
AC_OUTPUT
