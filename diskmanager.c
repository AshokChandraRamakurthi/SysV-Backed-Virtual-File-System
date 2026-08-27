#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "vdisk.h"

// Bitmap helpers

// Set bit for block number bn in the bitmap (blocks 1-8 of VD)
static void bitmap_set(unsigned char *D, unsigned int bn)
{

    unsigned int byte_idx = bn / 8;
    unsigned int bit_idx = bn % 8;
    D[BLOCK_SIZE + byte_idx] |= (1u << (7 - bit_idx));
}

static void init_disk(unsigned char *D)
{
    unsigned int i;

    memset(D, 0, VD_SIZE);

    unsigned int nblocks = NBLOCKS;
    unsigned int nfreeblocks = NBLOCKS - 266;
    unsigned int root_block = ROOT_BLOCK;

    memcpy(D + 0, &nblocks, 4);
    memcpy(D + 4, &nfreeblocks, 4);
    memcpy(D + 8, &root_block, 4);

    for (i = 0; i <= 265; i++)
        bitmap_set(D, i);

    struct metadata dot;
    memset(&dot, 0, sizeof(dot));
    dot.type = 'd';
    strncpy(dot.name, ".", 22);
    dot.size = 2;
    dot.firstblock = ROOT_BLOCK;
    memcpy(D + (unsigned long)ROOT_BLOCK * BLOCK_SIZE, &dot, sizeof(dot));

    struct metadata dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    dotdot.type = 'd';
    strncpy(dotdot.name, "..", 22);
    dotdot.size = 0;
    dotdot.firstblock = ROOT_BLOCK;
    memcpy(D + (unsigned long)ROOT_BLOCK * BLOCK_SIZE + sizeof(dot),
           &dotdot, sizeof(dotdot));

    printf("+++ Virtual disk initialised\n");
    printf("+++ Number of blocks      = %u\n", nblocks);
    printf("+++ Number of free blocks = %u\n", nfreeblocks);
    printf("+++ Root directory block  = %u\n", root_block);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s create|remove\n", argv[0]);
        return 1;
    }

    key_t key = ftok(KEY_PATH, KEY_ID);
    if (key == (key_t)-1)
    {
        perror("ftok");
        return 1;
    }

    if (strcmp(argv[1], "create") == 0)
    {

        int shmid = shmget(key, VD_SIZE, IPC_CREAT | IPC_EXCL | 0666);
        if (shmid < 0)
        {
            perror("shmget (segment may already exist; run 'remove' first)");
            return 1;
        }

        unsigned char *D = (unsigned char *)shmat(shmid, NULL, 0);
        if (D == (unsigned char *)-1)
        {
            perror("shmat");
            return 1;
        }

        init_disk(D);

        shmdt(D);
        printf("+++ Shared memory segment created (shmid=%d)\n", shmid);
    }
    else if (strcmp(argv[1], "remove") == 0)
    {
        int shmid = shmget(key, VD_SIZE, 0666);
        if (shmid < 0)
        {
            perror("shmget (segment not found)");
            return 1;
        }
        if (shmctl(shmid, IPC_RMID, NULL) < 0)
        {
            perror("shmctl");
            return 1;
        }
        printf("+++ Virtual disk removed\n");
    }
    else
    {
        fprintf(stderr, "Unknown mode '%s'. Use 'create' or 'remove'.\n", argv[1]);
        return 1;
    }

    return 0;
}