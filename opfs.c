/*
 * opfs: a simple utility for manipulating xv6 file system images
 * Copyright (c) 2015 Takuo Watanabe
 */

/* usage: opfs img_file command [arg...]
 * command
 *     diskinfo
 *     info path
 *     ls path
 *     get path
 *     put path
 *     rm path
 *     cp spath dpath
 *     mv spath dpath
 *     ln spath dpath
 *     mkdir path
 *     rmdir path
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


/* img file structure
 *
 *    0    1    2         m-1   m         d-1   d        l-1    l         N-1
 * +----+----+----+-...-+----+----+-...-+----+----+-...-+----+----+-...-+----+
 * | BB | SB | IB | ... | IB | MB | ... | MB | DB | ... | DB | LB | ... | LB |
 * +----+----+----+-...-+----+----+-...-+----+----+-...-+----+----+-...-+----+
 *
 *           |<---- Ni ----->|<---- Nm ----->|<---- Nd ----->|<---- Nl ----->|
 *
 * BB: boot block   [0, 0]
 * SB: super block  [1, 1]
 * IB: inode block  [2, 2 - 1 + Ni]
 * MB: bitmap block [m, m - 1 + Nm]   (m = Nb + Ns + Ni)
 * DB: data block   [d, d - 1 + Nd]   (d = Nb + Ns + Ni + Nm)
 * LB: log block    [l, l - 1 + Nl]   (l = Nb + Ns + Ni + Nm + Nd = N - Nl)
 *
 * N = sb.size = Nb + Ns + Ni + Nm + Nd + Nl          (# of all blocks)
 * Nb = 1                                             (# of boot block)
 * Ns = 1                                             (# of super block)
 * Ni = sb.ninodes / IPB + 1                          (# of inode blocks)
 * Nm = N / (BSIZE * 8) + 1                           (# of bitmap blocks)
 * Nd = sb.nblocks = N - (Nb + Ns + Ni + Nm + Nl)     (# of data blocks)
 * Nl = sb.nlog                                       (# of log blocks)
 *
 * BSIZE = 512
 * IPB = BSIZE / sizeof(struct dinode) = 512 / 64 = 8
 *
 * Example: fs.img
 * BB: boot block   [0, 0]      = [0x00000000, 0x000001ff]
 * SB: super block  [1, 1]      = [0x00000200, 0x000003ff]
 * IB: inode block  [2, 27]     = [0x00000400, 0x000037ff]
 * MB: bitmap block [28, 28]    = [0x00003800, 0x000039ff]
 * DB: data block   [29, 993]   = [0x00003a00, 0x0007c3ff]
 * LB: log block    [994, 1023] = [0x0007c400, 0x0007ffff]
 *
 * N  = 1024
 * Ni = 200 / 8 + 1 = 26
 * Nm = 1024 / (512 * 8) + 1 = 1
 * Nd = sb.nblocks = 1024 - (1 + 1 + 26 + 1 + 30) = 965
 * Nl = sb.nlog = 30
 *
 */

/* dinode structure
 *
 * |<--- 32 bit ---->|
 * 
 * +--------+--------+
 * |  type  |  major |  file type, major device number [ushort * 2]
 * +--------+--------+
 * |  minor |  nlink |  minor device number, # of links [ushort * 2]
 * +--------+--------+
 * |      size       |  size of the file (bytes) [uint]
 * +-----------------+  \
 * |    addrs[0]     |  |
 * +-----------------+  |
 * |       :         |   > direct data block addresses (NDIRECT=12) [uint]
 * +-----------------+  |
 * | addrs[NDIRECT-1]|  |
 * +-----------------+  /
 * 
 */


/*
 * General mathematical functions
 */

static inline int min(int x, int y) {
    return x < y ? x : y;
}

static inline int max(int x, int y) {
    return x > y ? x : y;
}

// ceiling(x / y) where x >=0, y >= 0
static inline int divceil(int x, int y) {
    return x == 0 ? 0 : (x - 1) / y + 1;
}

// the number of 1s in a 32-bit unsigned integer
static uint bitcount(uint x) {
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0f0f0f0f;
    x = x + (x >> 8);
    x = x + (x >> 16);
    return x & 0x3f;
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

char *typename(int type) {
    switch (type) {
    case T_DIR:
        return "directory";
    case T_FILE:
        return "file";
    case T_DEV:
        return "device";
    default:
        return "unknown";
    }
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
                bp[bi / 8] |= m;
                if (!valid_data_block(img, b + bi)) {
                    fatal("balloc: %u: invalid data block number\n", b + bi);
                    return 0; // dummy
                }
                memset(img[b + bi], 0, BSIZE);
                return b + bi;
            }
        }
    }
    fatal("balloc: no free blocks\n");
    return 0; // dummy
}

// frees the block specified by b
int bfree(img_t img, uint b) {
    if (!valid_data_block(img, b)) {
        derror("bfree: %u: invalid data block number\n", b);
        return -1;
    }
    uchar *bp = img[BBLOCK(b, SBLK(img)->ninodes)];
    int bi = b % BPB;
    int m = 1 << (bi % 8);
    if ((bp[bi / 8] & m) == 0)
        dwarn("bfree: %u: already freed block\n", b);
    bp[bi / 8] &= ~m;
    return 0;
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

// truncate the file specified by ip to size
int itruncate(img_t img, inode_t ip, uint size) {
    if (ip->type == T_DEV)
        return -1;
    if (size > MAXFILESIZE)
        return -1;

    if (size < ip->size) {
        int n = divceil(ip->size, BSIZE);  // # of used blocks
        int k = divceil(size, BSIZE);      // # of blocks to keep
        int nd = min(n, NDIRECT);          // # of used direct blocks
        int kd = min(k, NDIRECT);          // # of direct blocks to keep
        for (int i = kd; i < nd; i++) {
            bfree(img, ip->addrs[i]);
            ip->addrs[i] = 0;
        }

        if (n > NDIRECT) {
            uint iaddr = ip->addrs[NDIRECT];
            assert(iaddr != 0);
            uint *iblock = (uint *)img[iaddr];
            int ni = max(n - NDIRECT, 0);  // # of used indirect blocks
            int ki = max(k - NDIRECT, 0);  // # of indirect blocks to keep
            for (uint i = ki; i < ni; i++) {
                bfree(img, iblock[i]);
                iblock[i] = 0;
            }
            if (ki == 0) {
                bfree(img, iaddr);
                ip->addrs[NDIRECT] = 0;
            }
        }
    }
    else {
        int n = size - ip->size; // # of bytes to be filled
        for (uint off = ip->size, t = 0, m = 0; t < n; t += m, off += m) {
            uchar *bp = img[bmap(img, ip, off / BSIZE)];
            m = min(n - t, BSIZE - off % BSIZE);
            memset(bp + off % BSIZE, 0, m);
        }
    }
    ip->size = size;
    return 0;
}


/*
 * Pathname handling functions
 */

// check if s is an empty string
static inline bool is_empty(char *s) {
    return *s == 0;
}

// check if c is a path separator
static inline bool is_sep(char c) {
    return c == '/';
}

// adapted from skipelem in xv6/fs.c
char *skipelem(char *path, char *name) {
    while (is_sep(*path))
        path++;
    char *s = path;
    while (!is_empty(path) && !is_sep(*path))
        path++;
    int len = min(path - s, DIRSIZ);
    memmove(name, s, len);
    if (len < DIRSIZ)
        name[len] = 0;
    return path;
}

// split the path into directory name and base name
char *splitpath(char *path, char *dirbuf, uint size) {
    char *s = path, *t = path;
    while (!is_empty(path)) {
        while (is_sep(*path))
            path++;
        s = path;
        while (!is_empty(path) && !is_sep(*path))
            path++;
    }
    if (dirbuf != NULL) {
        int n = min(s - t, size - 1);
        memmove(dirbuf, t, n);
        dirbuf[n] = 0;
    }
    return s;
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

// create a link to the parent directory
int dmkparlink(img_t img, inode_t pip, inode_t cip) {
    if (pip->type != T_DIR) {
        derror("dmkparlink: %d: not a directory\n", geti(img, pip));
        return -1;
    }
    if (cip->type != T_DIR) {
        derror("dmkparlink: %d: not a directory\n", geti(img, cip));
        return -1;
    }
    uint off;
    dlookup(img, cip, "..", &off);
    struct dirent de;
    de.inum = geti(img, pip);
    strncpy(de.name, "..", DIRSIZ);
    if (iwrite(img, cip, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
        derror("dmkparlink: write error\n");
        return -1;
    }
    pip->nlink++;
    return 0;
}


// returns the inode number of a file (rp/path)
inode_t ilookup(img_t img, inode_t rp, char *path) {
    char name[DIRSIZ + 1];
    name[DIRSIZ] = 0;
    while (true) {
        assert(path != NULL && rp != NULL && rp->type == T_DIR);
        path = skipelem(path, name);
        // if path is empty (or a sequence of path separators),
        // it should specify the root direcotry (rp) itself
        if (is_empty(name))
            return rp;

        inode_t ip = dlookup(img, rp, name, NULL);
        if (ip == NULL)
            return NULL;
        if (is_empty(path))
            return ip;
        if (ip->type != T_DIR) {
            derror("ilookup: %s: not a directory\n", name);
            return NULL;
        }
        rp = ip;
    }
}

// create a file
inode_t icreat(img_t img, inode_t rp, char *path, uint type, inode_t *dpp) {
    char name[DIRSIZ + 1];
    name[DIRSIZ] = 0;
    while (true) {
        assert(path != NULL && rp != NULL && rp->type == T_DIR);
        path = skipelem(path, name);
        if (is_empty(name)) {
            derror("icreat: %s: empty file name\n", path);
            return NULL;
        }

        inode_t ip = dlookup(img, rp, name, NULL);
        if (is_empty(path)) {
            if (ip != NULL) {
                derror("icreat: %s: file exists\n", name);
                return NULL;
            }
            ip = ialloc(img, type);
            daddent(img, rp, name, ip);
            if (ip->type == T_DIR) {
                daddent(img, ip, ".", ip);
                daddent(img, ip, "..", rp);
            }
            if (dpp != NULL)
                *dpp = rp;
            return ip;
        }
        if (ip == NULL || ip->type != T_DIR) {
            derror("icreat: %s: no such directory\n", name);
            return NULL;
        }
        rp = ip;
    }
}

// checks if dp is an empty directory
bool emptydir(img_t img, inode_t dp) {
    int nent = 0;
    struct dirent de;
    for (uint off = 0; off < dp->size; off += sizeof(de)) {
        iread(img, dp, (uchar *)&de, sizeof(de), off);
        if (de.inum != 0)
            nent++;
    }
    return nent == 2;
}

// unlinks a file (dp/path)
int iunlink(img_t img, inode_t rp, char *path) {
    char name[DIRSIZ + 1];
    name[DIRSIZ] = 0;
    while (true) {
        assert(path != NULL && rp != NULL && rp->type == T_DIR);
        path = skipelem(path, name);
        if (is_empty(name)) {
            derror("iunlink: empty file name\n");
            return -1;
        }
        uint off;
        inode_t ip = dlookup(img, rp, name, &off);
        if (ip != NULL && is_empty(path)) {
            if (strncmp(name, ".", DIRSIZ) == 0 ||
                strncmp(name, "..", DIRSIZ) == 0) {
                derror("iunlink: cannot unlink \".\" or \"..\"\n");
                return -1;
            }
            // erase the directory entry
            uchar zero[sizeof(struct dirent)];
            memset(zero, 0, sizeof(zero));
            if (iwrite(img, rp, zero, sizeof(zero), off) != sizeof(zero)) {
                derror("iunlink: write error\n");
                return -1;
            }
            if (ip->type == T_DIR && dlookup(img, ip, "..", NULL) == rp)
                rp->nlink--;
            ip->nlink--;
            if (ip->nlink == 0) {
                if (ip->type != T_DEV)
                    itruncate(img, ip, 0);
                ifree(img, geti(img, ip));
            }
            return 0;
        }
        if (ip == NULL || ip->type != T_DIR) {
            derror("iunlink: %s: no such directory\n", name);
            return -1;
        }
        rp = ip;
    }
}


/*
 * Command implementations
 */

// diskinfo
int do_diskinfo(img_t img, int argc, char *argv[]) {
    if (argc != 0) {
        error("usage: %s img_file diskinfo\n", progname);
        return EXIT_FAILURE;
    }

    uint N = SBLK(img)->size;
    uint Ni = SBLK(img)->ninodes / IPB + 1;
    uint Nm = N / (BSIZE * 8) + 1;
    uint Nd = SBLK(img)->nblocks;
    uint Nl = SBLK(img)->nlog;

    printf("total blocks: %d (%d bytes)\n", N, N * BSIZE);
    printf("inode blocks: #%d-#%d (%d blocks, %d inodes)\n",
           2, Ni + 1, Ni, SBLK(img)->ninodes);
    printf("bitmap blocks: #%d-#%d (%d blocks)\n", Ni + 2, Ni + Nm + 1, Nm);
    printf("data blocks: #%d-#%d (%d blocks)\n",
           Ni + Nm + 2, Ni + Nm + Nd + 1, Nd);
    printf("log blocks: #%d-#%d (%d blocks)\n",
           Ni + Nm + Nd + 2, Ni + Nm + Nd + Nl + 1, Nl);
    printf("maximum file size (bytes): %ld\n", MAXFILESIZE);

    int nblocks = 0;
    for (uint b = Ni + 2; b <= Ni + Nm + 1; b++)
        for (int i = 0; i < BSIZE; i++)
            nblocks += bitcount(img[b][i]);
    printf("# of used blocks: %d\n", nblocks);

    int n_dirs = 0, n_files = 0, n_devs = 0;
    for (uint b = 2; b <= Ni + 1; b++)
        for (int i = 0; i < IPB; i++)
            switch (((inode_t)img[b])[i].type) {
            case T_DIR:
                n_dirs++;
                break;
            case T_FILE:
                n_files++;
                break;
            case T_DEV:
                n_devs++;
                break;
            }
    printf("# of used inodes: %d (dirs: %d, files: %d, devs: %d)\n",
           n_dirs + n_files + n_devs, n_dirs, n_files, n_devs);

    return EXIT_SUCCESS;
}

// info path
int do_info(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file info path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];

    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        error("info: no such file or directory: %s\n", path);
        return EXIT_FAILURE;
    }
    printf("inode: %d\n", geti(img, ip));
    printf("type: %d (%s)\n", ip->type, typename(ip->type));
    printf("nlink: %d\n", ip->nlink);
    printf("size: %d\n", ip->size);
    if (ip->size > 0) {
        printf("data blocks:");
        int bcount = 0;
        for (uint i = 0; i < NDIRECT && ip->addrs[i] != 0; i++, bcount++)
            printf(" %d", ip->addrs[i]);
        uint iaddr = ip->addrs[NDIRECT];
        if (iaddr != 0) {
            bcount++;
            printf(" %d", iaddr);
            uint *iblock = (uint *)img[iaddr];
            for (int i = 0; i < BSIZE / sizeof(uint) && iblock[i] != 0;
                 i++, bcount++)
                printf(" %d", iblock[i]);
        }
        printf("\n");
        printf("# of data blocks: %d\n", bcount);
    }
    return EXIT_SUCCESS;
}

// ls path
int do_ls(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file ls path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];
    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        error("ls: %s: no such file or directory\n", path);
        return EXIT_FAILURE;
    }
    if (ip->type == T_DIR) {
        struct dirent de;
        for (uint off = 0; off < ip->size; off += sizeof(de)) {
            if (iread(img, ip, (uchar *)&de, sizeof(de), off) != sizeof(de)) {
                error("ls: %s: read error\n", path);
                return EXIT_FAILURE;
            }
            if (de.inum == 0)
                continue;
            char name[DIRSIZ + 1];
            name[DIRSIZ] = 0;
            strncpy(name, de.name, DIRSIZ);
            inode_t p = iget(img, de.inum);
            printf("%s %d %d %d\n", name, p->type, de.inum, p->size);
        }
    }
    else
        printf("%s %d %d %d\n", path, ip->type, geti(img, ip), ip->size);

    return EXIT_SUCCESS;
}

// get path
int do_get(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file get path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];

    // source
    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        error("get: no such file or directory: %s\n", path);
        return EXIT_FAILURE;
    }

    uchar buf[BUFSIZE];
    for (uint off = 0; off < ip->size; off += BUFSIZE) {
        int n = iread(img, ip, buf, BUFSIZE, off);
        if (n < 0) {
            error("get: %s: read error\n", path);
            return EXIT_FAILURE;
        }
        write(1, buf, n);
    }

    return EXIT_SUCCESS;
}

// put path
int do_put(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file put path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];

    // destination
    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        ip = icreat(img, root_inode, path, T_FILE, NULL);
        if (ip == NULL) {
            error("put: %s: cannot create\n", path);
            return EXIT_FAILURE;
        }
    }
    else {
        if (ip->type != T_FILE) {
            error("put: %s: directory or device\n", path);
            return EXIT_FAILURE;
        }
        itruncate(img, ip, 0);
    }
    
    uchar buf[BUFSIZE];
    for (uint off = 0; off < MAXFILESIZE; off += BUFSIZE) {
        int n = read(0, buf, BUFSIZE);
        if (n < 0) {
            perror(NULL);
            return EXIT_FAILURE;
        }
        if (iwrite(img, ip, buf, n, off) != n) {
            error("put: %s: write error\n", path);
            return EXIT_FAILURE;
        }
        if (n < BUFSIZE)
            break;
    }
    return EXIT_SUCCESS;
}

// rm path
int do_rm(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file rm path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];
    
    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        error("rm: %s: no such file or directory\n", path);
        return EXIT_FAILURE;
    }
    if (ip->type == T_DIR) {
        error("rm: %s: a directory\n", path);
        return EXIT_FAILURE;
    }
    if (iunlink(img, root_inode, path) < 0) {
        error("rm: %s: cannot unlink\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// cp src_path dest_path
int do_cp(img_t img, int argc, char *argv[]) {
    if (argc != 2) {
        error("usage: %s img_file cp spath dpath\n", progname);
        return EXIT_FAILURE;
    }
    char *spath = argv[0];
    char *dpath = argv[1];

    // source
    inode_t sip = ilookup(img, root_inode, spath);
    if (sip == NULL) {
        error("cp: %s: no such file or directory\n", spath);
        return EXIT_FAILURE;
    }
    if (sip->type != T_FILE) {
        error("cp: %s: directory or device file\n", spath);
        return EXIT_FAILURE;
    }

    // destination
    inode_t dip = ilookup(img, root_inode, dpath);
    char ddir[BUFSIZE];
    char *dname = splitpath(dpath, ddir, BUFSIZE);
    if (dip == NULL) {
        if (is_empty(dname)) {
            error("cp: %s: no such directory\n", dpath);
            return EXIT_FAILURE;
        }
        inode_t ddip = ilookup(img, root_inode, ddir);
        if (ddip == NULL) {
            error("cp: %s: no such directory\n", ddir);
            return EXIT_FAILURE;
        }
        if (ddip->type != T_DIR) {
            error("cp: %s: not a directory\n", ddir);
            return EXIT_FAILURE;
        }
        dip = icreat(img, ddip, dname, T_FILE, NULL);
        if (dip == NULL) {
            error("cp: %s/%s: cannot create\n", ddir, dname);
            return EXIT_FAILURE;
        }
    }
    else {
        if (dip->type == T_DIR) {
            char *sname = splitpath(spath, NULL, 0);
            inode_t fp = icreat(img, dip, sname, T_FILE, NULL);
            if (fp == NULL) {
                error("cp: %s/%s: cannot create\n", dpath, sname);
                return EXIT_FAILURE;
            }
            dip = fp;
        }
        else if (dip->type == T_FILE) {
            itruncate(img, dip, 0);
        }
        else if (dip->type == T_DEV) {
            error("cp: %s: device file\n", dpath);
            return EXIT_FAILURE;
        }
    }

    // sip : source file inode, dip : destination file inode
    uchar buf[BUFSIZE];
    for (uint off = 0; off < sip->size; off += BUFSIZE) {
        int n = iread(img, sip, buf, BUFSIZE, off);
        if (n < 0) {
            error("cp: %s: read error\n", spath);
            return EXIT_FAILURE;
        }
        if (iwrite(img, dip, buf, n, off) != n) {
            error("cp: %s: write error\n", dpath);
            return EXIT_FAILURE;
        }
    }
    
    return EXIT_SUCCESS;
}

// mv src_path dest_path
int do_mv(img_t img, int argc, char *argv[]) {
    if (argc != 2) {
        error("usage: %s img_file mv spath dpath\n", progname);
        return EXIT_FAILURE;
    }
    char *spath = argv[0];
    char *dpath = argv[1];

    // source
    inode_t sip = ilookup(img, root_inode, spath);
    if (sip == NULL) {
        error("mv: %s: no such file or directory\n", spath);
        return EXIT_FAILURE;
    }
    if (sip == root_inode) {
        error("mv: %s: root directory\n", spath);
        return EXIT_FAILURE;
    }

    inode_t dip = ilookup(img, root_inode, dpath);
    char ddir[BUFSIZE];
    char *dname = splitpath(dpath, ddir, BUFSIZE);
    if (dip != NULL) {
        if (dip->type == T_DIR) {
            char *sname = splitpath(spath, NULL, 0);
            inode_t ip = dlookup(img, dip, sname, NULL);
            // ip : inode of dpath/sname
            if (ip != NULL) {
                if (ip->type == T_DIR) {
                    // override existing empty directory
                    if (sip->type != T_DIR) {
                        error("mv: %s: not a directory\n", spath);
                        return EXIT_FAILURE;
                    }
                    if (!emptydir(img, ip)) {
                        error("mv: %s/%s: not empty\n", ddir, sname);
                        return EXIT_FAILURE;
                    }
                    iunlink(img, dip, sname);
                    daddent(img, dip, sname, sip);
                    iunlink(img, root_inode, spath);
                    dmkparlink(img, dip, sip);
                    return EXIT_SUCCESS;
                }
                else if (ip->type == T_FILE) {
                    // override existing file
                    if (sip->type != T_FILE) {
                        error("mv: %s: directory or device\n", spath);
                        return EXIT_FAILURE;
                    }
                    iunlink(img, dip, sname);
                    daddent(img, dip, sname, sip);
                    iunlink(img, root_inode, spath);
                    return EXIT_SUCCESS;
                }
                else {
                    error("mv: %s: device\n", dpath);
                    return EXIT_FAILURE;
                }
            }
            else { // ip == NULL
                daddent(img, dip, sname, sip);
                iunlink(img, root_inode, spath);
                if (sip->type == T_DIR)
                    dmkparlink(img, dip, sip);
            }
        }
        else if (dip->type == T_FILE) {
            // override existing file
            if (sip->type != T_FILE) {
                error("mv: %s: not a file\n", spath);
                return EXIT_FAILURE;
            }
            iunlink(img, root_inode, dpath);
            inode_t ip = ilookup(img, root_inode, ddir);
            assert(ip != NULL && ip->type == T_DIR);
            daddent(img, ip, dname, sip);
            iunlink(img, root_inode, spath);
        }
        else { // dip->type == T_DEV
            error("mv: %s: device\n", dpath);
            return EXIT_FAILURE;
        }
    }
    else { // dip == NULL
        if (is_empty(dname)) {
            error("mv: %s: no such directory\n", dpath);
            return EXIT_FAILURE;
        }
        inode_t ip = ilookup(img, root_inode, ddir);
        if (ip == NULL) {
            error("mv: %s: no such directory\n", ddir);
            return EXIT_FAILURE;
        }
        if (ip->type != T_DIR) {
            error("mv: %s: not a directory\n", ddir);
            return EXIT_FAILURE;
        }
        daddent(img, ip, dname, sip);
        iunlink(img, root_inode, spath);
        if (sip->type == T_DIR)
            dmkparlink(img, ip, sip);
    }
    return EXIT_SUCCESS;
}

// ln src_path dest_path
int do_ln(img_t img, int argc, char *argv[]) {
    if (argc != 2) {
        error("usage: %s img_file ln spath dpath\n", progname);
        return EXIT_FAILURE;
    }
    char *spath = argv[0];
    char *dpath = argv[1];

    // source
    inode_t sip = ilookup(img, root_inode, spath);
    if (sip == NULL) {
        error("ln: %s: no such file or directory\n", spath);
        return EXIT_FAILURE;
    }
    if (sip->type != T_FILE) {
        error("ln: %s: is a directory or a device\n", spath);
        return EXIT_FAILURE;
    }

    // destination
    char ddir[BUFSIZE];
    char *dname = splitpath(dpath, ddir, BUFSIZE);
    inode_t dip = ilookup(img, root_inode, ddir);
    if (dip == NULL) {
        error("ln: %s: no such directory\n", ddir);
        return EXIT_FAILURE;
    }
    if (dip->type != T_DIR) {
        error("ln: %s: not a directory\n", ddir);
        return EXIT_FAILURE;
    }
    if (is_empty(dname)) {
        dname = splitpath(spath, NULL, 0);
        if (dlookup(img, dip, dname, NULL) != NULL) {
            error("ln: %s/%s: file exists\n", ddir, dname);
            return EXIT_FAILURE;
        }
    }
    else {
        inode_t ip = dlookup(img, dip, dname, NULL);
        if (ip != NULL) {
            if (ip->type != T_DIR) {
                error("ln: %s/%s: file exists\n", ddir, dname);
                return EXIT_FAILURE;
            }
            dname = splitpath(spath, NULL, 0);
            dip = ip;
        }
    }
    if (daddent(img, dip, dname, sip) < 0) {
        error("ln: %s/%s: cannot create a link\n", ddir, dname);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// mkdir path
int do_mkdir(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file mkdir path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];
    
    if (ilookup(img, root_inode, path) != NULL) {
        error("mkdir: %s: file exists\n", path);
        return EXIT_FAILURE;
    }
    if (icreat(img, root_inode, path, T_DIR, NULL) == NULL) {
        error("mkdir: %s: cannot create\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// rmdir path
int do_rmdir(img_t img, int argc, char *argv[]) {
    if (argc != 1) {
        error("usage: %s img_file rmdir path\n", progname);
        return EXIT_FAILURE;
    }
    char *path = argv[0];
    
    inode_t ip = ilookup(img, root_inode, path);
    if (ip == NULL) {
        error("rmdir: %s: no such file or directory\n", path);
        return EXIT_FAILURE;
    }
    if (ip->type != T_DIR) {
        error("rmdir: %s: not a directory\n", path);
        return EXIT_FAILURE;
    }
    if (!emptydir(img, ip)) {
        error("rmdir: %s: non-empty directory\n", path);
        return EXIT_FAILURE;
    }
    if (iunlink(img, root_inode, path) < 0) {
        error("rmdir: %s: cannot unlink\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}


struct cmd_table_ent {
    char *name;
    char *args;
    int (*fun)(img_t, int, char **);
};

struct cmd_table_ent cmd_table[] = {
    { "diskinfo", "", do_diskinfo },
    { "info", "path", do_info },
    { "ls", "path", do_ls },
    { "get", "path", do_get },
    { "put", "path", do_put },
    { "rm", "path", do_rm },
    { "cp", "spath dpath", do_cp },
    { "mv", "spath dpath", do_mv },
    { "ln", "spath dpath", do_ln },
    { "mkdir", "path", do_mkdir },
    { "rmdir", "path", do_rmdir },
    { NULL, NULL }
};

int exec_cmd(img_t img, char *cmd, int argc, char *argv[]) {
    for (int i = 0; cmd_table[i].name != NULL; i++) {
        if (strcmp(cmd, cmd_table[i].name) == 0)
            return cmd_table[i].fun(img, argc, argv);
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
