# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile: Makefile for use with GNU make.
#
# This file is part of timesignal.
#
# Copyright © 2025 James Seo <james@equiv.tech>

PKG_CONFIG        ?= $(shell command -v pkg-config > /dev/null && echo pkg-config)

ifeq (,$(PKG_CONFIG))
$(error "Cannot find pkg-config.")
endif

PREFIX            ?= /usr
SBIN              := $(PREFIX)/sbin

TARGET            := timesignal
BUILDDIR          := build
SRCDIR            := src
INCDIR            := include

CC                := gcc

CFLAGS            ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS            += -I$(INCDIR)
CFLAGS            += $(shell $(PKG_CONFIG) --cflags libpipewire-0.3)

LDFLAGS           ?=
LDFLAGS           += $(shell $(PKG_CONFIG) --libs alsa)
LDFLAGS           += $(shell $(PKG_CONFIG) --libs libpipewire-0.3)

SRC               := $(wildcard $(SRCDIR)/*.c)
OBJ               := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC))

all:              $(TARGET)

debug:            CFLAGS := $(filter-out -O2,$(CFLAGS)) -O0 -g -DTSIG_DEBUG
debug:            clean $(TARGET)

$(TARGET):        $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o:  $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install:          $(TARGET)
	install -d $(DESTDIR)$(SBIN)
	install -D -m 755 $(TARGET) $(DESTDIR)$(SBIN)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(SBIN)/$(TARGET)

.PHONY:           all debug clean install uninstall
