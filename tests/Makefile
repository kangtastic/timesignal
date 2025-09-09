# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile: tests subdirectory Makefile for use with GNU make.
#
# This file is part of timesignal.
#
# Copyright © 2025 James Seo <james@equiv.tech>

BUILDDIR          := build
SRCDIR            := ../src
INCDIR            := ../include

CMOCKADIR         := cmocka
CMOCKABUILDDIR    := $(CMOCKADIR)/build
CMOCKAINCDIR      := $(CMOCKADIR)/include

CC                ?= gcc

CFLAGS            ?= -O0 -g -Wall -Wextra -Wformat -Werror=format-security \
                     -fno-omit-frame-pointer -fstack-protector-strong \
                     -fsanitize=address -fPIE -std=gnu11
CFLAGS            += -I$(SRCDIR) -I$(INCDIR) -I$(CMOCKADIR)/include

LDFLAGS           ?= -pie -Wl,-z,relro -Wl,-z,now -fsanitize=address
LDFLAGS           += -L$(CMOCKABUILDDIR)/src -Wl,-rpath=$(CMOCKABUILDDIR)/src
LDFLAGS           += -lcmocka

all:

clean:
	rm -rf $(BUILDDIR)

distclean:        clean
	rm -rf $(CMOCKABUILDDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o:  %.c | $(BUILDDIR) $(CMOCKABUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CMOCKABUILDDIR):
	mkdir -p $(CMOCKABUILDDIR)
	@(cd $(CMOCKABUILDDIR) && cmake ../ && make -s)

.PHONY:           all clean distclean
