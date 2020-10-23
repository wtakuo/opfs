opfs
========
A set of simple utilities for manipulating [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) file system images

## Installation

Simply invoking `make` should build all the things: `opfs`, `newfs`, and `modfs`.
```
    $ make
```
You can copy these executables to your favorite place.
Alternatively, you can invoke the target `install` of `Makefile` with the specification of `PREFIX` as follows.

```
    $ sudo make PREFIX=/usr/local install
```

## Usage
This package provides three commands: `opfs`, `newfs` and `modfs`.

### 1. opfs
The command `opfs` provides safe operations on an xv6 file system in the disk image file (_imgfile_).

<pre>
opfs <i>imgfile</i> <i>command</i>
</pre>

_Command_ is one of the following:

* `diskinfo` : displays the information of the file system in the disk image file
* `info` _path_ : displays the detailed information of a file specified by _path_
* `ls` _path_ : lists the contents of a directory specified by _path_
* `get` _path_ : copies the contents of a file specified by _path_ to the standard output
* `put` _path_ : copies the standard input to a file specified by _path_
* `rm` _path_ : removes a file specified by _path_
* `cp` _spath_ _dpath_ : copies the contents of a file specified by _spath_ to the destination specified by _dpath_
* `mv` _spath_ _dpath_ : moves (renames) a file specified by _spath_ to the destination specified by _dpath_
* `ln` _spath_ _dpath_ : creates a new directory entry specified by _dpath_ which points to the same file specified by _spath_
* `mkdir` _path_ : creates a new directory specified by _path_
* `rmdir` _path_ : removes an empty directory specified by _path_

#### Examples
Display the information of the file system in `fs.img`.
```
$ opfs fs.img diskinfo
magic: 10203040
total blocks: 1000 (1024000 bytes)
log blocks: #2-#31 (30 blocks)
inode blocks: #32-#44 (13 blocks, 200 inodes)
bitmap blocks: #45-#45 (1 blocks)
data blocks: #46-#999 (954 blocks)
maximum file size (bytes): 274432
# of used blocks: 594
# of used inodes: 19 (dirs: 1, files: 17, devs: 1)
```

List the contents of the root directory of the file system in `fs.img`.
```
$ opfs fs.img ls /
. 1 1 1024
.. 1 1 1024
README 2 2 2059
cat 2 3 23888
echo 2 4 22720
forktest 2 5 13080
grep 2 6 27248
init 2 7 23824
kill 2 8 22696
ln 2 9 22648
ls 2 10 26120
mkdir 2 11 22792
rm 2 12 22784
sh 2 13 41656
stressfs 2 14 23792
usertests 2 15 152224
grind 2 16 37928
wc 2 17 25032
zombie 2 18 22184
console 3 19 0
```

Copy the contents of the file `README` in `fs.img` into the file `README_xv6.txt` in the host OS file system.
```
$ opfs fs.img get README > REAMDE_xv6.txt
```

### 2. newfs
The command `newfs` creates a new empty disk image file named _imgfile_.
<pre>
newfs <i>imgfile</i> <i>size</i> <i>ninodes</i> <i>nlog</i>
</pre>

* _size_ : number of all blocks
* _ninodes_ : number of i-nodes
* _nlog_ : number of log blocks

#### Example
Create a new empty disk image file named `fs0.img`.
```
$ newfs fs0.img 1000 200 30
# of blocks: 1000
# of inodes: 200
# of log blocks: 30
# of inode blocks: 13
# of bitmap blocks: 1
# of data blocks: 954
```

### 3. modfs
The command `modfs` provides potentially unsafe operations on an xv6 file system in the disk image file (_imgfile_).

<pre>
modfs <i>imgfile</i> <i>command</i>
</pre>

_Command_ is one of the following:

* `superblock.magic` [_val_] : the `magic` field of the superblock
* `superblock.size` [_val_] : the `size` field of the superblock (total number of blocks)
* `superblock.nblocks` [_val_] : the `nblocks` field of the superblock (number of data blocks)
* `superblock.ninodes` [_val_] : the `ninodes` field of the superblock (number of i-nodes)
* `superblock.nlog` [_val_] : the `nlog` field of the superblock (number of log blocks)
* `superblock.logstart` [_val_] : the `logstart` field of the superblock (starting block number of log blocks)
* `superblock.inodestart` [_val_] : the `inodestart` field of the superblock (starting number of i-node blocks)
* `superblock.bmapstart` [_val_] : the `bmapstart` field of the superblock (starting block number of bitmap blocks)
* `bitmap` _bnum_ [_val_] : the _bnum_-th value of the bitmap (0 or 1)
* `inode.type` _inum_ [_val_] : the `type` field of the _inum_-th i-node
* `inode.nlink` _inum_ [_val_] : the `nlink` field of the _inum_-th i-node
* `inode.size` _inum_ [_val_] : the `size` field of the _inum_-th i-node
* `inode.addrs` _inum_ _n_ [_val_] : the block number of the _n_-th data block referred from the _inum_-th i-node
* `inode.indirect` _inum_ [_val_] : the block number of the indirect block referred from the _inum_-th i-node
* `dirent` _path_ _name_ [_val_] : the i-node number of the entry _name_ of the directory specified by _path_

In each command, providing optional parameter _val_ modifies the specified value.
Be aware that such modification may break the consistency of the file system.