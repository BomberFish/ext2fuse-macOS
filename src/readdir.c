#include "readdir.h"
#include "ext2fs.h"

// For R_OK flags:
#include <fcntl.h>

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize)
{
    if(off < bufsize)
        return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
    else
        return fuse_reply_buf(req, NULL, 0);
}

static void dirbuf_add(struct dirbuf *b, const char *name, ext2_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

static int walk_dir(struct ext2_dir_entry *de, int offset, int blocksize,
        char *buf, void *priv_data)
{
    struct dirbuf *b = (struct dirbuf *) priv_data;
    char *s;
    int name_len = de->name_len & 0xFF;

    s = malloc(name_len + 1);
    if(!s)
        return -ENOMEM;

    memcpy(s, de->name, name_len);
    s[name_len] = '\0';

    dirbuf_add(b, s, de->inode);
    free(s);
    return 0;
}

void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                             off_t off, struct fuse_file_info *fi)
{
    errcode_t rc;
    struct dirbuf b;

    dbg("op_readdir(req, ino %d, size %d, off, fuse_file_info *)", (int) ino, size);
    // need read permissions on the directory
    rc = check_perms(fuse_req_ctx(req), EXT2FS_INO(ino), R_OK);
    if (rc)
    {   
        fuse_reply_err(req, rc);
        return;
    }

    memset(&b, 0, sizeof(b));

    rc = do_dir_iterate(EXT2FS_INO(ino), 0, walk_dir, &b);
    if (rc) {
        fuse_reply_err(req, EIO);
        free(b.p);
        return;
    }

    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
}


