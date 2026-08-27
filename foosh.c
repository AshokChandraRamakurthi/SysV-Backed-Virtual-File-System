#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdisk.h"
#include "diskutils.c"

static unsigned int CBN;
static char CWD[4096];

static void cmd_cd(const char *arg)
{
    const char *target = (arg && arg[0]) ? arg : "/";

    if (strcmp(target, "/") == 0)
    {
        CBN = RBN;
        strcpy(CWD, "");
        return;
    }

    unsigned int new_bn = vd_resolve_dir(CBN, target);
    if (new_bn == 0)
    {
        fprintf(stderr, "*** Error: unable to change to directory %s\n", target);
        return;
    }

    if (target[0] == '/')
    {

        strncpy(CWD, target, sizeof(CWD) - 1);

        size_t l = strlen(CWD);
        if (l > 1 && CWD[l - 1] == '/')
            CWD[--l] = '\0';

        if (CWD[l - 1] == '/')
            CWD[--l] = '\0';
    }
    else
    {

        char buf[4096];
        strncpy(buf, target, sizeof(buf) - 1);
        char *tok = strtok(buf, "/");
        while (tok)
        {
            if (strcmp(tok, ".") == 0)
            {
            }
            else if (strcmp(tok, "..") == 0)
            {
                char *sl = strrchr(CWD, '/');
                if (sl)
                    *sl = '\0';
                else
                    CWD[0] = '\0';
            }
            else
            {
                size_t l = strlen(CWD);
                snprintf(CWD + l, sizeof(CWD) - l - 1, "/%s", tok);
            }
            tok = strtok(NULL, "/");
        }
    }

    CBN = new_bn;
}

#define MAX_ARGS 8

static int tokenise(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max)
    {

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
        if (*p)
            *p++ = '\0';
    }
    return argc;
}

static const char *hd_path(const char *arg)
{
    return (arg && arg[0] == '`') ? arg + 1 : NULL;
}

static void cmd_cp(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "*** cp: usage: cp src dst\n");
        return;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    const char *hd_src = hd_path(src);
    const char *hd_dst = hd_path(dst);

    if (hd_src && !hd_dst)
    {

        vd_cp_hd_to_vd(CBN, hd_src, dst);
    }
    else if (!hd_src && hd_dst)
    {

        vd_cp_vd_to_hd(CBN, src, hd_dst);
    }
    else if (!hd_src && !hd_dst)
    {

        vd_cp_vd_to_vd(CBN, src, dst);
    }
    else
    {
        fprintf(stderr, "*** cp: HD-to-HD copy not supported\n");
    }
}

static void print_prompt(void)
{
    printf("[foosh] VD:%s> ", CWD);
    fflush(stdout);
}

int main(void)
{
    if (joindisk() < 0)
    {
        fprintf(stderr, "*** Cannot attach to virtual disk. Run diskmanager create first.\n");
        return 1;
    }

    printf("+++ Number of blocks = %u\n", NBLOCKS_G);
    printf("+++ Number of free blocks = %u\n", NFREEBLOCKS);
    printf("+++ First block of the root directory = %u\n", RBN);

    CBN = RBN;
    CWD[0] = '\0';

    char line[1024];
    char *argv[MAX_ARGS];

    while (1)
    {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin))
            break;

        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n')
            line[--l] = '\0';
        if (l == 0)
            continue;

        int argc = tokenise(line, argv, MAX_ARGS);
        if (argc == 0)
            continue;

        const char *cmd = argv[0];

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
        {
            break;
        }
        else if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "chdir") == 0)
        {
            cmd_cd(argc > 1 ? argv[1] : NULL);
        }
        else if (strcmp(cmd, "md") == 0 || strcmp(cmd, "mkdir") == 0)
        {
            if (argc < 2)
            {
                fprintf(stderr, "*** md: argument required\n");
            }
            else
                vd_mkdir(CBN, argv[1]);
        }
        else if (strcmp(cmd, "dir") == 0)
        {
            vd_dir(CBN);
        }
        else if (strcmp(cmd, "ls") == 0)
        {
            vd_ls(CBN, argc > 1 ? argv[1] : NULL);
        }
        else if (strcmp(cmd, "cp") == 0 || strcmp(cmd, "copy") == 0)
        {
            cmd_cp(argc, argv);
        }
        else if (strcmp(cmd, "prn") == 0 || strcmp(cmd, "type") == 0)
        {
            if (argc < 2)
            {
                fprintf(stderr, "*** prn: argument required\n");
            }
            else
                vd_prn(CBN, argv[1]);
        }
        else
        {
            fprintf(stderr, "*** Unknown command: %s\n", cmd);
        }
    }

    printf("+++ Number of free blocks = %u\n", NFREEBLOCKS);
    leavedisk();
    return 0;
}