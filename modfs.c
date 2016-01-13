/*
 * opfs: a simple utility for manipulating xv6 file system images
 * Copyright (c) 2015, 2016 Takuo Watanabe
 */

/* usage: ckfs img_file command [arg...]
 * command
 *     superblock.size [val]
 *     superblock.nblocks [val]
 *     superblock.ninodes [val]
 *     superblock.nlog [val]
 *     superblock.logstart [val]
 *     superblock.inodestart [val]
 *     superblock.bmapstart [val]
 *     bitmap bnum [val]
 *     inode.type inum [val]
 *     inode.nlink inum [val]
 *     inode.size inum [val]
 *     inode.addrs inum n [val]
 *     inode.indirect inum [val]
 *     dirent path name [val]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>

#include "libfs.h"

// superblock.FIELD [val]
int do_superblock(img_t img, int argc, char *argv[], char *field) {
    uint *f = NULL;
    if (strcmp(field, "size") == 0)
        f = &SBLK(img)->size;
    else if (strcmp(field, "nblocks") == 0)
        f = &SBLK(img)->nblocks;
    else if (strcmp(field, "ninodes") == 0)
        f = &SBLK(img)->ninodes;
    else if (strcmp(field, "nlog") == 0)
        f = &SBLK(img)->nlog;
    else if (strcmp(field, "logstart") == 0)
        f = &SBLK(img)->logstart;
    else if (strcmp(field, "inodestart") == 0)
        f = &SBLK(img)->inodestart;
    else if (strcmp(field, "bmapstart") == 0)
        f = &SBLK(img)->bmapstart;
    else {
        error("no such field in superblock: %s\n", field);
        return EXIT_FAILURE;
    }
    if (argc > 1) {
        error("usage: %s img_file superblock.%s [val]\n", progname, field);
        return EXIT_FAILURE;
    }
    if (argc == 0)
        printf("%u\n", *f);
    else
        *f = atoi(argv[0]);
    return EXIT_SUCCESS;
}

// bitmap bnum [val]
int do_bitmap(img_t img, int argc, char *argv[], char *field) {
    if (argc < 1 || argc > 2) {
        error("usage: %s img_file bitmap [val]\n", progname);
        return EXIT_FAILURE;
    }
    uint bnum = atoi(argv[0]);
    if (bnum >= SBLK(img)->size) {
        error("bitmap: %u: invalid block number\n", bnum);
        return EXIT_FAILURE;
    }
    uchar *bp = img[BBLOCK(bnum, SBLKS(img))];
    int bi = bnum % BPB;
    int m = 1 << (bi % 8);

    if (argc == 1)
        printf("%d\n", (bp[bi / 8] & m) > 0 ? 1 : 0);
    else { // argc == 2
        int val = atoi(argv[1]);
        if (val == 0)
            bp[bi / 8] &= ~m;
        else if (val == 1)
            bp[bi / 8] |= m;
        else {
            error("bitmap: val must be 0 or 1\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

// inode.FIELD inum [args...]
int do_inode(img_t img, int argc, char *argv[], char *field) {
    if (argc < 1)
        goto usage;
    uint inum = atoi(argv[0]);
    if (inum < 1 || inum >= SBLK(img)->ninodes) {
        error("inode: %u: invalid inode number\n", inum);
        return EXIT_FAILURE;
    }
    inode_t ip = iget(img, inum);

    if (strcmp(field, "type") == 0) {
        if (argc == 1)
            printf("%d\n", ip->type);
        else if (argc == 2)
            ip->type = atoi(argv[1]);
        else
            goto usage;
    }
    else if (strcmp(field, "nlink") == 0) {
        if (argc == 1)
            printf("%d\n", ip->nlink);
        else if (argc == 2)
            ip->nlink = atoi(argv[1]);
        else
            goto usage;
    }
    else if (strcmp(field, "size") == 0) {
        if (argc == 1)
            printf("%d\n", ip->size);
        else if (argc == 2)
            ip->size = atoi(argv[1]);
        else
            goto usage;
    }
    else if (strcmp(field, "indirect") == 0) {
        if (argc == 1)
            printf("%d\n", ip->addrs[NDIRECT]);
        else if (argc == 2)
            ip->addrs[NDIRECT] = atoi(argv[1]);
        else
            goto usage;
    }
    else if (strcmp(field, "addrs") == 0) {
        if (argc < 2)
            goto usage;
        uint n = atoi(argv[1]);
        if (n < NDIRECT) {
            if (argc == 2)
                printf("%d\n", ip->addrs[n]);
            else if (argc == 3)
                ip->addrs[n] = atoi(argv[2]);
            else
                goto usage;
        }
        else if (NDIRECT <= n && n < NDIRECT + NINDIRECT) {
            uint b = ip->addrs[NDIRECT];
            if (!valid_data_block(img, b)) {
                error("inode: %u: not a valid data block\n", b);
                return EXIT_FAILURE;
            }
            uint *ib = (uint *)img[b];
            if (argc == 2)
                printf("%d\n", ib[n - NDIRECT]);
            else if (argc == 3)
                ib[n - NDIRECT] = atoi(argv[2]);
            else
                goto usage;
        }
    }
    else
        assert(false);
    
    return EXIT_SUCCESS;

 usage:
    error("usage: %s img_file inode.%s inum %s\n", progname, field,
          strcmp(field, "addrs") == 0 ? "n [val]" : "[val]");
    return EXIT_FAILURE;
}

// dirent path name [val]
int do_dirent(img_t img, int argc, char *argv[], char *field) {
    if (argc < 2 || argc > 3) {
        error("usage: %s img_file dirent path name [val]\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];
    char *name = argv[1];

    inode_t dp = ilookup(img, root_inode, path);
    if (dp == NULL) {
        error("dirent: %s: no such directory\n", path);
        return EXIT_FAILURE;
    }
    if (dp->type != T_DIR) {
        error("dirent: %s: not a directory\n", path);
        return EXIT_FAILURE;
    }

    uint off;
    inode_t ip = dlookup(img, dp, name, &off);

    if (argc == 2) {
        if (ip == NULL) {
            error("dirent: %s: no such file or directory\n", name);
            return EXIT_FAILURE;
        }
        printf("%d\n", geti(img, ip));
        
    }
    else {
        if (strcmp(argv[2], "delete") == 0) {
            uchar zero[sizeof(struct dirent)];
            memset(zero, 0, sizeof(zero));
            if (iwrite(img, dp, zero, sizeof(zero), off) != sizeof(zero)) {
                error("dirent: %s: write error\n", name);
                return EXIT_FAILURE;
            }
        }
        else {
            uint inum = atoi(argv[2]);
            if (ip == NULL) {
                error("dirent: %s: no such file or directory\n", name);
                return EXIT_FAILURE;
            }
            struct dirent de;
            if (iread(img, dp, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
                error("dirent: %s: read error\n", name);
                return EXIT_FAILURE;
            }
            de.inum = inum;
            if (iwrite(img, dp, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
                error("dirent: %s: write error\n", name);
                return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}


struct cmd_table_ent {
    char *name;
    char *args;
    int (*fun)(img_t, int, char **, char*);
    char *field;
};

struct cmd_table_ent cmd_table[] = {
    { "superblock.size", "[val]", do_superblock, "size" },
    { "superblock.nblocks", "[val]", do_superblock, "nblocks" },
    { "superblock.ninodes", "[val]", do_superblock, "ninodes" },
    { "superblock.nlog", "[val]", do_superblock, "nlog" },
    { "superblock.logstart", "[val]", do_superblock, "logstart" },
    { "superblock.inodestart", "[val]", do_superblock, "inodestart" },
    { "superblock.bmapstart", "[val]", do_superblock, "bmapstart" },
    { "bitmap", "bnum [val]", do_bitmap, NULL },
    { "inode.type", "inum [val]", do_inode, "type" },
    { "inode.nlink", "inum [val]", do_inode, "nlink" },
    { "inode.size", "inum [val]", do_inode, "size" },
    { "inode.addrs", "inum n [val]", do_inode, "addrs" },
    { "inode.indirect", "inum [val]", do_inode, "indirect" },
    { "dirent", "path name [val]", do_dirent, NULL },
    { NULL, NULL }
};

int exec_cmd(img_t img, char *cmd, int argc, char *argv[]) {
    for (int i = 0; cmd_table[i].name != NULL; i++) {
        if (strcmp(cmd, cmd_table[i].name) == 0)
            return cmd_table[i].fun(img, argc, argv, cmd_table[i].field);
    }
    error("unknown command: %s\n", cmd);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    progname = argv[0];
    if (argc < 3) {
        error("usage: %s img_file command [arg...]\n", progname);
        error("Commands are:\n");
        for (int i = 0; cmd_table[i].name != NULL; i++)
            error("    %s %s\n", cmd_table[i].name, cmd_table[i].args);
        return EXIT_FAILURE;
    }
    char *img_file = argv[1];
    char *cmd = argv[2];

    int img_fd = open(img_file, O_RDWR);
    if (img_fd < 0) {
        perror(img_file);
        return EXIT_FAILURE;
    }

    struct stat img_sbuf;
    if (fstat(img_fd, &img_sbuf) < 0) {
        perror(img_file);
        close(img_fd);
        return EXIT_FAILURE;
    }
    size_t img_size = (size_t)img_sbuf.st_size;

    img_t img = mmap(NULL, img_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, img_fd, 0);
    if (img == MAP_FAILED) {
        perror(img_file);
        close(img_fd);
        return EXIT_FAILURE;
    }

    root_inode = iget(img, root_inode_number);

    // shift argc and argv to point the first command argument
    int status = EXIT_FAILURE;
    if (setjmp(fatal_exception_buf) == 0)
        status = exec_cmd(img, cmd, argc - 3, argv + 3);

    munmap(img, img_size);
    close(img_fd);

    return status;
}

/* For Emacs
 * Local Variables: ***
 * c-file-style: "gnu" ***
 * c-basic-offset: 4 ***
 * End: ***
 */
