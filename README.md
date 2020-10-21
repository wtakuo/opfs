opfs
========
A simple utility for manipulating xv6-riscv file system images

## Installation

```
    cd /path-to-opfs
    make
    make install
```

## Usage

Opfs provides three commands: `opfs`, `newfs` and `modfs'.

`opfs` safely operates on a disk image.
```
    opfs img_file command [args]
```
Commands are one of the following:

* `diskinfo` displays the information of the disk image
* `info` displays the detailed information of a file
* `ls` lists the contents of a directory
* `get` copies a file to standard output
* `put` copies standard input to a file
* `rm` removes a file
* `cp` copies files
* `mv` moves (rename) files
* `ln` makes link
* `mkdir` makes a directory
* `rmdir` removes an empty directory 


`newfs` creates a new empty disk image file of name `img_file`.
```
    newfs img_file size ninodes nlog
```

`modfs` unsafely modifies a disk image.
```
    modfs img_file command [arg...]
```

