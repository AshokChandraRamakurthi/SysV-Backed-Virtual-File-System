#ifndef VDISK_H
#define VDISK_H

#include <sys/types.h>

#define BLOCK_SIZE 1024u
#define NBLOCKS 65536u
#define VD_SIZE ((size_t)NBLOCKS * BLOCK_SIZE)

#define SUPERBLOCK 0u
#define BITMAP_START 1u
#define BITMAP_BLOCKS 8u
#define FAT_START 9u
#define FAT_BLOCKS 256u
#define ROOT_BLOCK 265u
#define DATA_START 266u

#define FAT_NULL 0u

#define ENTRIES_PER_BLOCK 32u

#define KEY_PATH "/tmp"
#define KEY_ID 'V'

struct metadata
{
    char type;
    char name[23];
    unsigned int size;
    unsigned int firstblock;
};

#endif