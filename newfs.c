/*
 * newfs: a simple utility for creating empty xv6 file system images
 * Copyright (c) 2015 Takuo Watanabe
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


// xv6 header files
#include "types.h"
#include "fs.h"

// file types (copied from xv6/stat.h)
// Including xv6/stat.h causes a name clash (with struct stat)
#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device

#define MAXFILESIZE (MAXFILE * BSIZE)
#define BUFSIZE 1024

/*
 * General mathematical functions
 */

static inline int min(int x, int y) {
    return x < y ? x : y;
}


/*
 * Debugging and reporting functions and macros
 */

// program name
char *progname;
jmp_buf fatal_exception_buf;

#define ddebug(...) debug_message("DEBUG", __VA_ARGS__)
#define derror(...) debug_message("ERROR", __VA_ARGS__)
#define dwarn(...) debug_message("WARNING", __VA_ARGS__)

void debug_message(const char *tag, const char *fmt, ...) {
#ifndef NDEBUG
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s: ", tag);
    vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}

void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "FATAL: ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    longjmp(fatal_exception_buf, 1);
}

/*
 * Basic operations on blocks
 */

// a disk image as an array of blocks
// a block is a uchar array of size BSIZE
typedef uchar (*img_t)[BSIZE];

// super block
#define SBLK(img) ((struct superblock *)(img)[1])

// checks if b is a valid data block number
static inline bool valid_data_block(img_t img, uint b) {
    uint Ni = SBLK(img)->ninodes / IPB + 1;       // # of inode blocks
    uint Nm = SBLK(img)->size / (BSIZE * 8) + 1;  // # of bitmap blocks
    uint Nd = SBLK(img)->nblocks;                 // # of data blocks
    uint d = 2 + Ni + Nm;                         // 1st data block number
    return d <= b && b <= d + Nd - 1;
}

// allocates a new data block and returns its block number
uint balloc(img_t img) {
    for (int b = 0; b < SBLK(img)->size; b += BPB) {
        uchar *bp = img[BBLOCK(b, SBLK(img)->ninodes)];
        for (int bi = 0; bi < BPB && b + bi < SBLK(img)->size; bi++) {
            int m = 1 << (bi % 8);
            if ((bp[bi / 8] & m) == 0) {
                if (!valid_data_block(img, b + bi)) {
                    fatal("balloc: %u: invalid data block number\n", b + bi);
                    return 0; // dummy
                }
                bp[bi / 8] |= m;
                memset(img[b + bi], 0, BSIZE);
                return b + bi;
            }
        }
    }
    fatal("balloc: no free blocks\n");
    return 0; // dummy
}

/*
 * Basic operations on files (inodes)
 */

// inode
typedef struct dinode *inode_t;

// inode of the root directory
const uint root_inode_number = 1;
inode_t root_inode;

// returns the pointer to the inum-th dinode structure
inode_t iget(img_t img, uint inum) {
    if (0 < inum && inum < SBLK(img)->ninodes)
        return (inode_t)img[IBLOCK(inum)] + inum % IPB;
    derror("iget: %u: invalid inode number\n", inum);
    return NULL;
}

// retrieves the inode number of a dinode structure
uint geti(img_t img, inode_t ip) {
    uint Ni = SBLK(img)->ninodes / IPB + 1;       // # of inode blocks
    for (int i = 0; i < Ni; i++) {
        inode_t bp = (inode_t)img[i + 2];
        if (bp <= ip && ip < bp + IPB)
            return ip - bp + i * IPB;
    }
    derror("geti: %p: not in the inode blocks\n", ip);
    return 0;
}

// allocate a new inode structure
inode_t ialloc(img_t img, uint type) {
    for (int inum = 1; inum < SBLK(img)->ninodes; inum++) {
        inode_t ip = (inode_t)img[IBLOCK(inum)] + inum % IPB;
        if (ip->type == 0) {
            memset(ip, 0, sizeof(struct dinode));
            ip->type = type;
            return ip;
        }
    }
    fatal("ialloc: cannot allocate\n");
    return NULL;
}

// frees inum-th inode
int ifree(img_t img, uint inum) {
    inode_t ip = iget(img, inum);
    if (ip == NULL)
        return -1;
    if (ip->type == 0)
        dwarn("ifree: inode #%d is already freed\n", inum);
    if (ip->nlink > 0)
        dwarn("ifree: nlink of inode #%d is not zero\n", inum);
    ip->type = 0;
    return 0;
}

// returns n-th data block number of the file specified by ip
uint bmap(img_t img, inode_t ip, uint n) {
    if (n < NDIRECT) {
        uint addr = ip->addrs[n];
        if (addr == 0) {
            addr = balloc(img);
            ip->addrs[n] = addr;
        }
        return addr;
    }
    else {
        uint k = n - NDIRECT;
        if (k >= NINDIRECT) {
            derror("bmap: %u: invalid index number\n", n);
            return 0;
        }
        uint iaddr = ip->addrs[NDIRECT];
        if (iaddr == 0) {
            iaddr = balloc(img);
            ip->addrs[NDIRECT] = iaddr;
        }
        uint *iblock = (uint *)img[iaddr];
        if (iblock[k] == 0)
            iblock[k] = balloc(img);
        return iblock[k];
    }
}

// reads n byte of data from the file specified by ip
int iread(img_t img, inode_t ip, uchar *buf, uint n, uint off) {
    if (ip->type == T_DEV)
        return -1;
    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > ip->size)
        n = ip->size - off;
    // t : total bytes that have been read
    // m : last bytes that were read
    uint t = 0;
    for (uint m = 0; t < n; t += m, off += m, buf += m) {
        uint b = bmap(img, ip, off / BSIZE);
        if (!valid_data_block(img, b)) {
            derror("iread: %u: invalid data block\n", b);
            break;
        }
        m = min(n - t, BSIZE - off % BSIZE);
        memmove(buf, img[b] + off % BSIZE, m);
    }
    return t;
}

// writes n byte of data to the file specified by ip
int iwrite(img_t img, inode_t ip, uchar *buf, uint n, uint off) {
    if (ip->type == T_DEV)
        return -1;
    if (off > ip->size || off + n < off || off + n > MAXFILESIZE)
        return -1;
    // t : total bytes that have been written
    // m : last bytes that were written
    uint t = 0;
    for (uint m = 0; t < n; t += m, off += m, buf += m) {
        uint b = bmap(img, ip, off / BSIZE);
        if (!valid_data_block(img, b)) {
            derror("iwrite: %u: invalid data block\n", b);
            break;
        }
        m = min(n - t, BSIZE - off % BSIZE);
        memmove(img[b] + off % BSIZE, buf, m);
    }
    if (t > 0 && off > ip->size)
        ip->size = off;
    return t;
}

/*
 * Operations on directories
 */

// search a file (name) in a directory (dp)
inode_t dlookup(img_t img, inode_t dp, char *name, uint *offp) {
    assert(dp->type == T_DIR);
    struct dirent de;
    for (uint off = 0; off < dp->size; off += sizeof(de)) {
        if (iread(img, dp, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
            derror("dlookup: %s: read error\n", name);
            return NULL;
        }
        if (strncmp(name, de.name, DIRSIZ) == 0) {
            if (offp != NULL)
                *offp = off;
            return iget(img, de.inum);
        }
    }
    return NULL;
}

// add a new directory entry in dp
int daddent(img_t img, inode_t dp, char *name, inode_t ip) {
    struct dirent de;
    uint off;
    // try to find an empty entry
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (iread(img, dp, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
            derror("daddent: %u: read error\n", geti(img, dp));
            return -1;
        }
        if (de.inum == 0)
            break;
        if (strncmp(de.name, name, DIRSIZ) == 0) {
            derror("daddent: %s: exists\n", name);
            return -1;
        }
    }
    strncpy(de.name, name, DIRSIZ);
    de.inum = geti(img, ip);
    if (iwrite(img, dp, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
        derror("daddent: %u: write error\n", geti(img, dp));
        return -1;
    }
    if (strncmp(name, ".", DIRSIZ) != 0)
        ip->nlink++;
    return 0;
}


int setupfs(img_t img, uint size, uint ninodes, uint nlog) {
    uint niblocks = ninodes / IPB + 1;
    uint nmblocks = size / (BSIZE * 8) + 1;
    uint nblocks = size - (2 + niblocks + nmblocks + nlog);

    printf("# of blocks: %u\n", size);
    printf("# of inodes: %u\n", ninodes);
    printf("# of log blocks: %u\n", nlog);
    
    printf("# of inode blocks: %u\n", niblocks);
    printf("# of bitmap blocks: %u\n", nmblocks);
    printf("# of data blocks: %u\n", nblocks);

    // clear all blocks
    memset((uchar *)img, 0, BSIZE * size);

    // setup superblock
    struct superblock sblk = { size, nblocks, ninodes, nlog };
    memmove(img[1], (uchar *)&sblk, sizeof(sblk));

    // setup initial bitmap
    const uint na = 2 + niblocks + nmblocks;
    for (uint b = 0; b < na; b += BPB) {
        uchar *bp = img[BBLOCK(b, ninodes)];
        for (int bi = 0; bi < BPB && b + bi < na; bi++) {
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
