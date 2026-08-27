#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "vdisk.h"

// Global variables
unsigned char *D;
unsigned int NBLOCKS_G;
unsigned int NFREEBLOCKS;
unsigned int RBN;

// Low-level disk operations

int joindisk(void)
{
    key_t key = ftok(KEY_PATH, KEY_ID);
    if (key == (key_t)-1)
    {
        perror("ftok");
        return -1;
    }

    int shmid = shmget(key, VD_SIZE, 0666);
    if (shmid < 0)
    {
        perror("shmget");
        return -1;
    }

    D = (unsigned char *)shmat(shmid, NULL, 0);
    if (D == (unsigned char *)-1)
    {
        perror("shmat");
        return -1;
    }

    memcpy(&NBLOCKS_G, D + 0, 4);
    memcpy(&NFREEBLOCKS, D + 4, 4);
    memcpy(&RBN, D + 8, 4);
    return 0;
}

void leavedisk(void)
{
    if (D)
        shmdt(D);
    D = NULL;
}

// Superblock operations

static void write_superblock(void)
{
    memcpy(D + 0, &NBLOCKS_G, 4);
    memcpy(D + 4, &NFREEBLOCKS, 4);
    memcpy(D + 8, &RBN, 4);
}

// Bitmap operations

static int bitmap_test(unsigned int bn)
{
    unsigned int byte_idx = bn / 8;
    unsigned int bit_idx = bn % 8;
    return (D[BLOCK_SIZE + byte_idx] >> (7 - bit_idx)) & 1;
}

static void bitmap_set_bit(unsigned int bn)
{
    unsigned int byte_idx = bn / 8;
    unsigned int bit_idx = bn % 8;
    D[BLOCK_SIZE + byte_idx] |= (1u << (7 - bit_idx));
}

static void bitmap_clear_bit(unsigned int bn)
{
    unsigned int byte_idx = bn / 8;
    unsigned int bit_idx = bn % 8;
    D[BLOCK_SIZE + byte_idx] &= ~(1u << (7 - bit_idx));
}

// FAT operations

static unsigned long fat_offset(unsigned int bn)
{

    unsigned long fat_block = FAT_START + bn / 256;
    unsigned long fat_index = bn % 256;
    return fat_block * BLOCK_SIZE + fat_index * 4;
}

static unsigned int fat_read(unsigned int bn)
{
    unsigned int val;
    memcpy(&val, D + fat_offset(bn), 4);
    return val;
}

static void fat_write(unsigned int bn, unsigned int next)
{
    memcpy(D + fat_offset(bn), &next, 4);
}

// Block allocation

unsigned int getfreeblock(void)
{
    memcpy(&NFREEBLOCKS, D + 4, 4);

    if (NFREEBLOCKS == 0)
    {
        fprintf(stderr, "*** Disk full\n");
        return 0;
    }

    srand((unsigned)time(NULL) ^ (unsigned)(size_t)D);

    unsigned int bn;
    do
    {
        bn = (unsigned int)(rand() % NBLOCKS_G);
    } while (bn < DATA_START || bitmap_test(bn));

    bitmap_set_bit(bn);
    fat_write(bn, FAT_NULL);

    memset(D + (unsigned long)bn * BLOCK_SIZE, 0, BLOCK_SIZE);

    NFREEBLOCKS--;
    write_superblock();
    return bn;
}

void freeblock(unsigned int bn)
{
    if (bn < DATA_START)
        return;
    memcpy(&NFREEBLOCKS, D + 4, 4);
    bitmap_clear_bit(bn);
    fat_write(bn, FAT_NULL);
    NFREEBLOCKS++;
    write_superblock();
}

static void free_chain(unsigned int bn)
{
    while (bn != FAT_NULL)
    {
        unsigned int next = fat_read(bn);
        freeblock(bn);
        bn = next;
    }
}

// Directory operations

static int dir_read_entry(unsigned int dir_bn, unsigned int n, struct metadata *out)
{
    unsigned int block = dir_bn;
    unsigned int per = ENTRIES_PER_BLOCK;
    unsigned int skip = n / per;

    for (unsigned int i = 0; i < skip; i++)
    {
        block = fat_read(block);
        if (block == FAT_NULL)
            return 0;
    }

    unsigned int idx = n % per;
    memcpy(out, D + (unsigned long)block * BLOCK_SIZE + idx * sizeof(struct metadata),
           sizeof(struct metadata));
    return 1;
}

static void dir_write_entry(unsigned int dir_bn, unsigned int n, const struct metadata *m)
{
    unsigned int block = dir_bn;
    unsigned int per = ENTRIES_PER_BLOCK;
    unsigned int skip = n / per;

    for (unsigned int i = 0; i < skip; i++)
    {
        unsigned int next = fat_read(block);
        if (next == FAT_NULL)
        {
            unsigned int nb = getfreeblock();
            if (nb == 0)
            {
                fprintf(stderr, "*** Out of disk space\n");
                return;
            }
            fat_write(block, nb);
            next = nb;
        }
        block = next;
    }

    unsigned int idx = n % per;
    memcpy(D + (unsigned long)block * BLOCK_SIZE + idx * sizeof(struct metadata),
           m, sizeof(struct metadata));
}

static unsigned int dir_size(unsigned int dir_bn)
{
    struct metadata dot;
    dir_read_entry(dir_bn, 0, &dot);
    return dot.size;
}

static void dir_increment_size(unsigned int dir_bn)
{
    struct metadata dot;
    dir_read_entry(dir_bn, 0, &dot);
    dot.size++;
    dir_write_entry(dir_bn, 0, &dot);
}

// Path resolution

static int resolve_path(unsigned int cbn, const char *path,
                        unsigned int *out_bn,
                        unsigned int *parent_bn,
                        char *leaf_name)
{
    char buf[4096];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    size_t len = strlen(buf);
    if (len > 1 && buf[len - 1] == '/')
        buf[--len] = '\0';

    unsigned int cur = (buf[0] == '/') ? RBN : cbn;
    if (parent_bn)
        *parent_bn = cur;
    if (leaf_name)
        leaf_name[0] = '\0';

    char *p = buf;
    if (*p == '/')
        p++;

    if (*p == '\0')
    {

        if (out_bn)
            *out_bn = RBN;
        if (parent_bn)
            *parent_bn = RBN;
        if (leaf_name)
            strcpy(leaf_name, "/");
        return 1;
    }

    char *tok = strtok(p, "/");
    while (tok)
    {
        char *next_tok = strtok(NULL, "/");

        if (parent_bn)
            *parent_bn = cur;
        if (leaf_name)
            strncpy(leaf_name, tok, 22);

        if (strcmp(tok, ".") == 0)
        {
            // do nothing
        }
        else if (strcmp(tok, "..") == 0)
        {
            // Entry 1 is ".."
            struct metadata dotdot;
            dir_read_entry(cur, 1, &dotdot);
            cur = dotdot.firstblock;
        }
        else
        {
            // Search entries
            unsigned int nsz = dir_size(cur);
            int found = 0;
            for (unsigned int i = 2; i < nsz; i++)
            {
                struct metadata m;
                dir_read_entry(cur, i, &m);
                if (strcmp(m.name, tok) == 0 ||
                    (m.type == 'd' && strncmp(m.name, tok, 22) == 0))
                {
                    if (next_tok == NULL)
                    {
                        // last component
                        if (out_bn)
                            *out_bn = m.firstblock;
                        return 1;
                    }
                    if (m.type != 'd')
                        return 0; // can't cd into file
                    cur = m.firstblock;
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                // Not found
                if (next_tok == NULL && out_bn)
                    *out_bn = 0;
                return 0;
            }
        }
        tok = next_tok;
    }

    // consume all tokens
    if (out_bn)
        *out_bn = cur;
    return 1;
}

// Directory lookup

static int dir_find(unsigned int dir_bn, const char *name)
{
    unsigned int nsz = dir_size(dir_bn);
    for (unsigned int i = 0; i < nsz; i++)
    {
        struct metadata m;
        dir_read_entry(dir_bn, i, &m);
        if (strncmp(m.name, name, 22) == 0)
            return (int)i;
    }
    return -1;
}

// Create directory

int vd_mkdir(unsigned int cbn, const char *path)
{
    char buf[4096];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    size_t len = strlen(buf);
    if (len > 1 && buf[len - 1] == '/')
    {
        buf[--len] = '\0';
    }

    char *slash = strrchr(buf, '/');
    char parent_path[4096] = ".";
    char new_name[24] = "";

    if (slash)
    {
        *slash = '\0';
        snprintf(parent_path, sizeof(parent_path), "%s", (buf[0] == '\0') ? "/" : buf);
        strncpy(new_name, slash + 1, 22);
    }
    else
    {
        snprintf(new_name, 23, "%s", buf);
    }

    if (new_name[0] == '\0')
    {
        fprintf(stderr, "*** mkdir: invalid directory name\n");
        return -1;
    }

    unsigned int parent_bn;
    if (!resolve_path(cbn, parent_path, &parent_bn, NULL, NULL))
    {
        fprintf(stderr, "*** mkdir: parent path not found: %s\n", parent_path);
        return -1;
    }

    if (dir_find(parent_bn, new_name) >= 0)
    {
        fprintf(stderr, "*** mkdir: '%s' already exists\n", new_name);
        return -1;
    }

    unsigned int new_bn = getfreeblock();
    if (new_bn == 0)
        return -1;

    struct metadata dot;
    memset(&dot, 0, sizeof(dot));
    dot.type = 'd';
    strncpy(dot.name, ".", 22);
    dot.size = 2;
    dot.firstblock = new_bn;
    dir_write_entry(new_bn, 0, &dot);

    struct metadata dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    dotdot.type = 'd';
    strncpy(dotdot.name, "..", 22);
    dotdot.size = 0;
    dotdot.firstblock = parent_bn;
    dir_write_entry(new_bn, 1, &dotdot);

    unsigned int parent_sz = dir_size(parent_bn);
    struct metadata entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = 'd';
    snprintf(entry.name, 23, "%s", new_name);
    entry.size = 2;
    entry.firstblock = new_bn;
    dir_write_entry(parent_bn, parent_sz, &entry);

    dir_increment_size(parent_bn);
    return 0;
}

// List directory names

void vd_dir(unsigned int cbn)
{
    unsigned int nsz = dir_size(cbn);
    unsigned int col = 0;

    for (unsigned int i = 0; i < nsz; i++)
    {
        struct metadata m;
        dir_read_entry(cbn, i, &m);

        char display[28];
        if (m.type == 'd')
            snprintf(display, sizeof(display), " %s/", m.name);
        else
            snprintf(display, sizeof(display), " %s", m.name);

        printf("%-16s", display);
        col++;
        if (col % 4 == 0)
            printf("\n");
    }
    if (col % 4 != 0)
        printf("\n");
}

// List directory contents

static void print_ls_header(unsigned int count)
{
    printf("Total %u entries\n", count);
    printf("-----------------------------------------------------------------------\n");
    printf("TYPE  %-22s  %-10s  %s\n", "NAME", "SIZE", "FIRST BLOCK");
    printf("-----------------------------------------------------------------------\n");
}

static void print_ls_footer(void)
{
    printf("-----------------------------------------------------------------------\n");
}

static void print_ls_entry(const struct metadata *m)
{
    char display[28];
    if (m->type == 'd')
        snprintf(display, sizeof(display), "%s/", m->name);
    else
        snprintf(display, sizeof(display), "%s", m->name);
    printf("   %c  %-22s  %-10u  %u\n", m->type, display, m->size, m->firstblock);
}

//  ls of a directory given its first block
static void ls_dir(unsigned int dir_bn)
{
    unsigned int nsz = dir_size(dir_bn);
    print_ls_header(nsz);
    for (unsigned int i = 0; i < nsz; i++)
    {
        struct metadata m;
        dir_read_entry(dir_bn, i, &m);
        print_ls_entry(&m);
    }
    print_ls_footer();
}

// vd_ls: list path (if empty/NULL, list cbn).
//  path can be a file or directory.
void vd_ls(unsigned int cbn, const char *path)
{
    if (!path || path[0] == '\0')
    {
        ls_dir(cbn);
        return;
    }

    unsigned int target_bn;
    if (!resolve_path(cbn, path, &target_bn, NULL, NULL))
    {
        fprintf(stderr, "*** ls: path not found: %s\n", path);
        return;
    }

    // Determine type
    char leaf[24] = "";
    unsigned int parent_bn;
    resolve_path(cbn, path, &target_bn, &parent_bn, leaf);

    int idx = dir_find(parent_bn, leaf);
    if (idx < 0)
    {
        ls_dir(RBN);
        return;
    }

    struct metadata m;
    dir_read_entry(parent_bn, (unsigned int)idx, &m);

    if (m.type == 'd')
    {
        ls_dir(m.firstblock);
    }
    else
    {
        printf("%u %c %u %s\n", m.firstblock, m.type, m.size, path);
    }
}

// Copy operations

static void write_file_data(unsigned int first_bn, const unsigned char *buf, unsigned int sz)
{
    unsigned int remaining = sz;
    unsigned int offset = 0;
    unsigned int cur = first_bn;

    while (remaining > 0)
    {
        unsigned int to_write = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        memcpy(D + (unsigned long)cur * BLOCK_SIZE, buf + offset, to_write);
        offset += to_write;
        remaining -= to_write;

        if (remaining > 0)
        {
            unsigned int nb = getfreeblock();
            if (nb == 0)
            {
                fprintf(stderr, "*** Out of disk space\n");
                return;
            }
            fat_write(cur, nb);
            cur = nb;
        }
    }
}

int vd_cp_hd_to_vd(unsigned int cbn, const char *hd_path, const char *vd_dest)
{
    FILE *f = fopen(hd_path, "rb");
    if (!f)
    {
        fprintf(stderr, "*** Error: Unable to read input file %s\n", hd_path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);

    unsigned char *buf = (unsigned char *)malloc(fsz > 0 ? fsz : 1);
    if (!buf)
    {
        fclose(f);
        fprintf(stderr, "*** malloc failed\n");
        return -1;
    }
    size_t _r = fread(buf, 1, (size_t)fsz, f);
    (void)_r;
    fclose(f);

    const char *hd_fname = strrchr(hd_path, '/');
    hd_fname = hd_fname ? hd_fname + 1 : hd_path;

    unsigned int dest_bn = 0;
    unsigned int parent_bn;
    char leaf[24];

    int resolved = resolve_path(cbn, vd_dest, &dest_bn, &parent_bn, leaf);

    char final_name[24];
    unsigned int dir_bn;

    if (resolved && dest_bn != 0)
    {
        struct metadata dm;
        int idx = dir_find(parent_bn, leaf);
        if (idx < 0)
        {
            dir_bn = dest_bn;
            strncpy(final_name, hd_fname, 22);
        }
        else
        {
            dir_read_entry(parent_bn, (unsigned int)idx, &dm);
            if (dm.type == 'd')
            {
                dir_bn = dm.firstblock;
                strncpy(final_name, hd_fname, 22);
            }
            else
            {
                dir_bn = parent_bn;
                snprintf(final_name, 24, "%s", leaf);

                free_chain(dm.firstblock);

                unsigned int first_bn = getfreeblock();
                if (first_bn == 0)
                {
                    free(buf);
                    return -1;
                }
                write_file_data(first_bn, buf, (unsigned int)fsz);

                dm.size = (unsigned int)fsz;
                dm.firstblock = first_bn;
                dir_write_entry(parent_bn, (unsigned int)idx, &dm);
                free(buf);
                return 0;
            }
        }
    }
    else
    {
        dir_bn = parent_bn;
        snprintf(final_name, 24, "%s", leaf);
    }

    int existing = dir_find(dir_bn, final_name);
    if (existing >= 0)
    {
        struct metadata em;
        dir_read_entry(dir_bn, (unsigned int)existing, &em);
        if (em.type == 'f')
        {
            free_chain(em.firstblock);
            unsigned int first_bn = getfreeblock();
            if (first_bn == 0)
            {
                free(buf);
                return -1;
            }
            write_file_data(first_bn, buf, (unsigned int)fsz);
            em.size = (unsigned int)fsz;
            em.firstblock = first_bn;
            dir_write_entry(dir_bn, (unsigned int)existing, &em);
            free(buf);
            return 0;
        }
    }

    unsigned int first_bn = getfreeblock();
    if (first_bn == 0)
    {
        free(buf);
        return -1;
    }
    write_file_data(first_bn, buf, (unsigned int)fsz);

    unsigned int dir_sz = dir_size(dir_bn);
    struct metadata nm;
    memset(&nm, 0, sizeof(nm));
    nm.type = 'f';
    snprintf(nm.name, 24, "%s", final_name);
    nm.size = (unsigned int)fsz;
    nm.firstblock = first_bn;
    dir_write_entry(dir_bn, dir_sz, &nm);
    dir_increment_size(dir_bn);

    free(buf);
    return 0;
}

int vd_cp_vd_to_hd(unsigned int cbn, const char *vd_src, const char *hd_dest)
{
    unsigned int src_bn, parent_bn;
    char leaf[24];

    if (!resolve_path(cbn, vd_src, &src_bn, &parent_bn, leaf))
    {
        fprintf(stderr, "*** cp: source not found: %s\n", vd_src);
        return -1;
    }

    int idx = dir_find(parent_bn, leaf);
    if (idx < 0)
    {
        fprintf(stderr, "*** cp: source not found: %s\n", vd_src);
        return -1;
    }

    struct metadata m;
    dir_read_entry(parent_bn, (unsigned int)idx, &m);

    if (m.type == 'd')
    {
        fprintf(stderr, "*** cp: cannot copy directory to HD\n");
        return -1;
    }

    FILE *f = fopen(hd_dest, "wb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }

    unsigned int remaining = m.size;
    unsigned int cur = m.firstblock;

    while (remaining > 0 && cur != FAT_NULL)
    {
        unsigned int to_write = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        fwrite(D + (unsigned long)cur * BLOCK_SIZE, 1, to_write, f);
        remaining -= to_write;
        cur = fat_read(cur);
    }
    fclose(f);
    return 0;
}

int vd_cp_vd_to_vd(unsigned int cbn, const char *vd_src, const char *vd_dest)
{
    unsigned int src_bn, src_parent;
    char src_leaf[24];

    if (!resolve_path(cbn, vd_src, &src_bn, &src_parent, src_leaf))
    {
        fprintf(stderr, "*** cp: source not found: %s\n", vd_src);
        return -1;
    }

    int sidx = dir_find(src_parent, src_leaf);
    if (sidx < 0)
    {
        fprintf(stderr, "*** cp: source not found: %s\n", vd_src);
        return -1;
    }

    struct metadata sm;
    dir_read_entry(src_parent, (unsigned int)sidx, &sm);

    if (sm.type == 'd')
    {
        fprintf(stderr, "*** cp: cannot copy directories\n");
        return -1;
    }

    unsigned char *buf = (unsigned char *)malloc(sm.size > 0 ? sm.size : 1);
    if (!buf)
    {
        fprintf(stderr, "*** malloc failed\n");
        return -1;
    }

    unsigned int remaining = sm.size;
    unsigned int cur = sm.firstblock;
    unsigned int offset = 0;

    while (remaining > 0 && cur != FAT_NULL)
    {
        unsigned int to_read = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        memcpy(buf + offset, D + (unsigned long)cur * BLOCK_SIZE, to_read);
        offset += to_read;
        remaining -= to_read;
        cur = fat_read(cur);
    }

    //  Determine destination
    unsigned int dest_bn, dest_parent;
    char dest_leaf[24];
    int dest_exists = resolve_path(cbn, vd_dest, &dest_bn, &dest_parent, dest_leaf);

    char final_name[24];
    unsigned int dir_bn;

    if (dest_exists && dest_bn != 0)
    {
        int didx = dir_find(dest_parent, dest_leaf);
        if (didx < 0)
        {
            // root
            dir_bn = dest_bn;
            snprintf(final_name, 24, "%s", sm.name);
        }
        else
        {
            struct metadata dm;
            dir_read_entry(dest_parent, (unsigned int)didx, &dm);
            if (dm.type == 'd')
            {
                dir_bn = dm.firstblock;
                snprintf(final_name, 24, "%s", sm.name);
            }
            else
            {
                //  Overwrite
                free_chain(dm.firstblock);
                unsigned int first_bn = getfreeblock();
                if (first_bn == 0)
                {
                    free(buf);
                    return -1;
                }
                write_file_data(first_bn, buf, sm.size);
                dm.size = sm.size;
                dm.firstblock = first_bn;
                dir_write_entry(dest_parent, (unsigned int)didx, &dm);
                free(buf);
                return 0;
            }
        }
    }
    else
    {
        dir_bn = dest_parent;
        snprintf(final_name, 24, "%s", dest_leaf);
    }

    // Check existing
    int existing = dir_find(dir_bn, final_name);
    if (existing >= 0)
    {
        struct metadata em;
        dir_read_entry(dir_bn, (unsigned int)existing, &em);
        if (em.type == 'f')
        {
            free_chain(em.firstblock);
            unsigned int first_bn = getfreeblock();
            if (first_bn == 0)
            {
                free(buf);
                return -1;
            }
            write_file_data(first_bn, buf, sm.size);
            em.size = sm.size;
            em.firstblock = first_bn;
            dir_write_entry(dir_bn, (unsigned int)existing, &em);
            free(buf);
            return 0;
        }
    }

    // Create new
    unsigned int first_bn = getfreeblock();
    if (first_bn == 0)
    {
        free(buf);
        return -1;
    }
    write_file_data(first_bn, buf, sm.size);

    unsigned int dir_sz = dir_size(dir_bn);
    struct metadata nm;
    memset(&nm, 0, sizeof(nm));
    nm.type = 'f';
    snprintf(nm.name, 24, "%s", final_name);
    nm.size = sm.size;
    nm.firstblock = first_bn;
    dir_write_entry(dir_bn, dir_sz, &nm);
    dir_increment_size(dir_bn);

    free(buf);
    return 0;
}

//  prn / type: print a VD file to stdout

void vd_prn(unsigned int cbn, const char *path)
{
    unsigned int src_bn, parent_bn;
    char leaf[24];

    if (!resolve_path(cbn, path, &src_bn, &parent_bn, leaf))
    {
        fprintf(stderr, "*** prn: not found: %s\n", path);
        return;
    }

    int idx = dir_find(parent_bn, leaf);
    if (idx < 0)
    {
        fprintf(stderr, "*** prn: not found: %s\n", path);
        return;
    }

    struct metadata m;
    dir_read_entry(parent_bn, (unsigned int)idx, &m);

    if (m.type == 'd')
    {
        fprintf(stderr, "*** prn: '%s' is a directory\n", path);
        return;
    }

    unsigned int remaining = m.size;
    unsigned int cur = m.firstblock;

    while (remaining > 0 && cur != FAT_NULL)
    {
        unsigned int to_print = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        fwrite(D + (unsigned long)cur * BLOCK_SIZE, 1, to_print, stdout);
        remaining -= to_print;
        cur = fat_read(cur);
    }
    printf("\n");
}

// Returns new block number, or 0 on error.

unsigned int vd_resolve_dir(unsigned int cbn, const char *path)
{
    unsigned int target_bn, parent_bn;
    char leaf[24];

    if (!path || path[0] == '\0' || strcmp(path, "/") == 0)
    {
        return RBN;
    }

    int ok = resolve_path(cbn, path, &target_bn, &parent_bn, leaf);
    if (!ok || target_bn == 0)
        return 0;

    if (strcmp(leaf, "/") == 0)
        return RBN;

    int idx = dir_find(parent_bn, leaf);
    if (idx < 0)
    {
        return target_bn;
    }

    struct metadata m;
    dir_read_entry(parent_bn, (unsigned int)idx, &m);

    if (m.type != 'd')
        return 0;
    return m.firstblock;
}