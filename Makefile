# opfs: a simple utility for manipulating xv6 file system images
# Copyright (c) 2015-2020 Takuo Watanabe

PREFIX = ~/.local
XV6HDRS = types.h fs.h
HDRS = libfs.h $(XV6HDRS)
SRCS = opfs.c newfs.c modfs.c libfs.c
OBJS = $(SRCS:%.c=%.o)
LIBS = libfs.o
EXES = opfs newfs modfs

TAGFILES = GTAGS GRTAGS GPATH

CC = gcc
WFLAGS =
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror $(WFLAGS) -g
CPPFLAGS = # -DNDEBUG
LDFLAGS =
OPTFLAGS = -O3

ifeq ($(shell uname),Darwin)
	CC = clang
endif
ifeq ($(shell uname),Linux)
	WFLAGS = -Wno-stringop-truncation 
endif

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
