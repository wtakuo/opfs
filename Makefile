# opfs: a simple utility for manipulating xv6 file system images
# Copyright (c) 2015 Takuo Watanabe


HDRS = $(XV6HDRS)
SRCS = opfs.c
OBJS = $(SRCS:%.c=%.o)
EXES = opfs

XV6HOME = $(HOME)/xv6
XV6HDRS = types.h fs.h

CC = clang
CFLAGS = -std=c99 -pedantic -Wall -Werror
CPPFLAGS = # -DNDEBUG
LDFLAGS =
OPTFLAGS =

RM = rm -f
CP = cp

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(OPTFLAGS) -c $<

.PHONY: all clean allclean

.PRECIOUS: %.o

all: $(EXES)

opfs: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) -o $@ $^

$(XV6HDRS): $(XV6HOME)
	$(CP) $(XV6HOME)/types.h .
	$(CP) $(XV6HOME)/fs.h .

clean:
	$(RM) $(EXES)
	$(RM) $(OBJS)

allclean: clean
	$(RM) $(XV6HDRS)
	$(RM) a.out *.o *~ .*~
