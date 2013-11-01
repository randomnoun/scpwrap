#
# Makefile for scpwrap
#
# $Id$
#

prefix = /usr/local
bindir = $(prefix)/bin
sharedir = $(prefix)/share
mandir = $(sharedir)/man
man1dir = $(mandir)/man1

CFLAGS = --std=c99

all: scpwrap

clean:
	rm -f scpwrap scpwrap.o

scpwrap: scpwrap.c
	gcc -Wl,--no-as-needed -lutil scpwrap.c -oscpwrap

install: all
	install scpwrap $(DESTDIR)$(bindir)
	install -m 0644 scpwrap.1 $(DESTDIR)$(man1dir)

.PHONY: all

