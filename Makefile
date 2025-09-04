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

HAVE_PIPEWIRE     := $(shell $(PKG_CONFIG) --exists libpipewire-0.3 && echo yes)
HAVE_PULSE        := $(shell $(PKG_CONFIG) --exists libpulse && echo yes)
HAVE_ALSA         := $(shell $(PKG_CONFIG) --exists alsa && echo yes)
HAVE_BACKENDS     := 0

ifeq (yes,$(HAVE_PIPEWIRE))
HAVE_BACKENDS          := $(shell echo $$(($(HAVE_BACKENDS)+1)))
endif

ifeq (yes,$(HAVE_PULSE))
HAVE_BACKENDS          := $(shell echo $$(($(HAVE_BACKENDS)+1)))
endif

ifeq (yes,$(HAVE_ALSA))
HAVE_BACKENDS          := $(shell echo $$(($(HAVE_BACKENDS)+1)))
endif

ifeq (0,$(HAVE_BACKENDS))
$(error "Cannot find libpipewire-0.3, libpulse, or alsa.")
endif

PREFIX            ?= /usr
SBIN              := $(PREFIX)/sbin

TARGET            := timesignal
BUILDDIR          := build
SRCDIR            := src
INCDIR            := include

CC                := gcc

CFLAGS            ?= -O2 -fstack-protector-strong -Wall -Wextra -Wformat -Werror=format-security -fPIE -std=gnu11
CFLAGS            += -I$(INCDIR)
CFLAGS_EXTRA      :=

LDFLAGS           ?= -pie -Wl,-z,relro -Wl,-z,now
LIBS              := -ldl

SRC               := $(wildcard $(SRCDIR)/*.c)
OBJ               := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC))

ifeq (yes,$(HAVE_PIPEWIRE))
CFLAGS_EXTRA      += $(shell $(PKG_CONFIG) --cflags libpipewire-0.3) -DTSIG_HAVE_PIPEWIRE
LIBS              += $(shell $(PKG_CONFIG) --libs-only-L --libs-only-other libpipewire-0.3)
else
SRC               := $(filter-out $(SRCDIR)/pipewire.c,$(SRC))
OBJ               := $(filter-out $(BUILDDIR)/pipewire.o,$(OBJ))
endif

ifeq (yes,$(HAVE_PULSE))
CFLAGS_EXTRA      += $(shell $(PKG_CONFIG) --cflags libpulse) -DTSIG_HAVE_PULSE
LIBS              += $(shell $(PKG_CONFIG) --libs-only-L --libs-only-other libpulse)
else
SRC               := $(filter-out $(SRCDIR)/pulse.c,$(SRC))
OBJ               := $(filter-out $(BUILDDIR)/pulse.o,$(OBJ))
endif

ifeq (yes,$(HAVE_ALSA))
CFLAGS_EXTRA      += -DTSIG_HAVE_ALSA
LIBS              += $(shell $(PKG_CONFIG) --libs-only-L --libs-only-other alsa)
else
SRC               := $(filter-out $(SRCDIR)/alsa.c,$(SRC))
OBJ               := $(filter-out $(BUILDDIR)/alsa.o,$(OBJ))
endif

ifeq (yes,$(shell [ $(HAVE_BACKENDS) -ge 2 ] && echo yes))
CFLAGS_EXTRA      += -DTSIG_HAVE_BACKENDS
endif

all:              $(TARGET)

debug:            CFLAGS := $(filter-out -O2,$(CFLAGS)) -fsanitize=address -fno-omit-frame-pointer -O0 -g -DTSIG_DEBUG
debug:            LDFLAGS += -fsanitize=address
debug:            clean $(TARGET)

$(TARGET):        $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

$(BUILDDIR)/%.o:  $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c $< -o $@

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
