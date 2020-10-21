# opfs: a simple utility for manipulating xv6 file system images
# Copyright (c) 2015-2020 Takuo Watanabe

ifeq ($(shell uname),Darwin)
	PREFIX = /opt/local
	CC = clang
else
	PREFIX = /usr/local
	CC = gcc
endif

HDRS = libfs.h $(XV6HDRS)
SRCS = opfs.c newfs.c modfs.c libfs.c
OBJS = $(SRCS:%.c=%.o)
LIBS = libfs.o
EXES = opfs newfs modfs

TAGFILES = GTAGS GRTAGS GPATH

XV6HOME = $(HOME)/xv6-riscv
XV6HDRS = types.h fs.h

CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -g
CPPFLAGS = # -DNDEBUG
LDFLAGS =
OPTFLAGS = -O3

INSTALL = install
RM = rm -f
CP = cp
GTAGS = gtags

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(OPTFLAGS) -c $<

.PHONY: all install tags clean allclean

.PRECIOUS: %.o

all: $(EXES)

opfs: opfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

newfs: newfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

modfs: modfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

$(XV6HDRS): $(XV6HOME)
	$(CP) $(XV6HOME)/kernel/types.h .
	$(CP) $(XV6HOME)/kernel/fs.h .

install: $(EXES)
	$(INSTALL) -d $(PREFIX)/bin
	$(INSTALL) $^ $(PREFIX)/bin

tags: $(HDRS) $(SRCS)
	$(GTAGS) -v

clean:
	$(RM) $(EXES)
	$(RM) $(OBJS)

allclean: clean
	$(RM) $(TAGFILES)
	$(RM) a.out *.o *~ .*~
