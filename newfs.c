/*
 * opfs: a simple utility for creating empty xv6 file system images
 * Copyright (c) 2015-2019 Takuo Watanabe
 */

/* usage: newfs img_file size ninodes nlog 
 *     size : total # of blocks
 *     ninodes : # of inodes
 *     nlog : # of log blocks
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

int setupfs(img_t img, uint size, uint ninodes, uint nlog) {
    uint niblocks = ninodes / IPB + 1;
    uint nmblocks = size / (BSIZE * 8) + 1;
    uint nblocks = size - (2 + nlog + niblocks + nmblocks);
    uint logstart = 2;
    uint inodestart = logstart + nlog;
    uint bmapstart = inodestart + niblocks;
    uint dstart = bmapstart + nmblocks;

    printf("# of blocks: %u\n", size);
    printf("# of inodes: %u\n", ninodes);
    printf("# of log blocks: %u\n", nlog);
    printf("# of inode blocks: %u\n", niblocks);
    printf("# of bitmap blocks: %u\n", nmblocks);
    printf("# of data blocks: %u\n", nblocks);

    // clear all blocks
    memset((uchar *)img, 0, BSIZE * size);

    // setup superblock
    struct superblock sblk = {
        FSMAGIC,
        size, nblocks, ninodes, nlog, logstart, inodestart, bmapstart
    };
    memmove(img[1], (uchar *)&sblk, sizeof(sblk));

    // setup initial bitmap
    for (uint b = 0; b < dstart; b += BPB) {
        uchar *bp = img[BBLOCK(b, SBLKS(img))];
        for (int bi = 0; bi < BPB && b + bi < dstart; bi++) {
            int m = 1 << (bi % 8);
            bp[bi / 8] |= m;
        }
    }

    // setup root directory
    root_inode = ialloc(img, T_DIR);
    assert(geti(img, root_inode) == root_inode_number);

    daddent(img, root_inode, ".", root_inode);
    daddent(img, root_inode, "..", root_inode);

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s file size ninodes nlog\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *file = argv[1];
    uint size = atoi(argv[2]);
    uint ninodes = atoi(argv[3]);
    uint nlog = atoi(argv[4]);

    int fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(file);
        return EXIT_FAILURE;
    }

    size_t img_size = BSIZE * size;
    lseek(fd, img_size - 1, SEEK_SET);
    char c = 0;
    write(fd, &c, 1);

    img_t img = mmap(NULL, img_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (img == MAP_FAILED) {
        perror(file);
        close(fd);
        return EXIT_FAILURE;
    }

    int status = EXIT_FAILURE;
    if (setjmp(fatal_exception_buf) == 0)
        status = setupfs(img, size, ninodes, nlog);

    munmap(img, img_size);
    close(fd);

    return status;
}

/* For Emacs
 * Local Variables: ***
 * c-file-style: "gnu" ***
 * c-basic-offset: 4 ***
 * End: ***
 */
