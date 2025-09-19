# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile: docs subdirectory Makefile for use with GNU make.
#
# This file is part of timesignal.
#
# Copyright © 2025 James Seo <james@equiv.tech>

PREFIX            ?= /usr
SHAREDIR          := $(PREFIX)/share
MAN1DIR           := $(SHAREDIR)/man/man1
MAN5DIR           := $(SHAREDIR)/man/man5

ETCDIR            ?= /etc

BUILDDIR          := build
INCDIR            := ../include

_DOCS             := $(wildcard *.in)
DOCS              := $(patsubst %.in,$(BUILDDIR)/%,$(_DOCS))

define get_default
$(shell awk '/TSIG_DEFAULTS_$(1)/ { gsub(/^"|"$$/, "", $$3); print $$3; }' $(INCDIR)/defaults.h)
endef

VERSION           ?= $(call get_default,VERSION)
URL               ?= $(call get_default,URL)

.PHONY:           all
all:              docs

.PHONY:           docs
docs:             $(DOCS)
docs:             $(BUILDDIR)/timesignal.1.gz
docs:             $(BUILDDIR)/timesignal.conf.5.gz

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%:    %.in | $(BUILDDIR)
	cp $< $@
	sed -i "s|__VERSION__|$(VERSION)|" $@
	sed -i "s|__URL__|$(URL)|" $@

$(BUILDDIR)/%.gz:  $(BUILDDIR)/% | $(BUILDDIR)
	gzip < $< > $@

.PHONY:            clean
clean:
	rm -rf $(BUILDDIR)

.PHONY:            install uninstall
install:           all
	install -D -m 644 $(BUILDDIR)/timesignal.1.gz $(DESTDIR)$(MAN1DIR)/timesignal.1.gz
	install -D -m 644 $(BUILDDIR)/timesignal.conf.5.gz $(DESTDIR)$(MAN5DIR)/timesignal.conf.5.gz
	install --backup=numbered -C -D -m 644 $(BUILDDIR)/timesignal.conf $(DESTDIR)$(ETCDIR)/timesignal.conf

uninstall:
	rm -f $(DESTDIR)$(MAN1DIR)/timesignal.1.gz
	rm -f $(DESTDIR)$(MAN5DIR)/timesignal.conf.5.gz
