#ifndef WIPE_BLOCK_H
#define WIPE_BLOCK_H

typedef int (*wipe_block_proc) (blk_t);

extern wipe_block_proc wipe_block_procedure;

#endif

