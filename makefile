CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS =

.PHONY: all compile clean

all: diskmanager foosh

compile: all

diskmanager: diskmanager.c vdisk.h
	$(CC) $(CFLAGS) -o diskmanager diskmanager.c $(LDFLAGS)

foosh: foosh.c diskutils.c vdisk.h
	$(CC) $(CFLAGS) -o foosh foosh.c $(LDFLAGS)

clean:
	rm -f diskmanager foosh *.o