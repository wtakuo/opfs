# opfs: a simple utility for manipulating xv6 file system images
# Copyright (c) 2015, 2016 Takuo Watanabe

HDRS = libfs.h $(XV6HDRS)
SRCS = opfs.c newfs.c modfs.c libfs.c
OBJS = $(SRCS:%.c=%.o)
LIBS = libfs.o
EXES = opfs newfs modfs

TAGFILES = GTAGS GRTAGS GPATH

XV6HOME = $(HOME)/xv6
XV6HDRS = types.h fs.h

CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Werror -g
CPPFLAGS = # -DNDEBUG
LDFLAGS =
OPTFLAGS =

RM = rm -f
CP = cp
GTAGS = gtags

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(OPTFLAGS) -c $<

.PHONY: all tags clean allclean

.PRECIOUS: %.o

all: $(EXES)

opfs: opfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

newfs: newfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

modfs: modfs.o $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

$(XV6HDRS): $(XV6HOME)
	$(CP) $(XV6HOME)/types.h .
	$(CP) $(XV6HOME)/fs.h .

tags: $(HDRS) $(SRCS)
	$(GTAGS) -v

clean:
	$(RM) $(EXES)
	$(RM) $(OBJS)

allclean: clean
	$(RM) $(XV6HDRS)
	$(RM) $(TAGFILES)
	$(RM) a.out *.o *~ .*~

