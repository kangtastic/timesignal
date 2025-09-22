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

.PHONY:           docs docs-html
docs:             $(DOCS)
docs:             $(BUILDDIR)/timesignal.1.gz
docs:             $(BUILDDIR)/timesignal.conf.5.gz

docs-html:        $(DOCS)
docs-html:        $(BUILDDIR)/timesignal.1.html
docs-html:        $(BUILDDIR)/timesignal.conf.5.html

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%:    %.in | $(BUILDDIR)
	cp $< $@
	sed -i "s|__VERSION__|$(VERSION)|" $@
	sed -i "s|__URL__|$(URL)|" $@

$(BUILDDIR)/%.gz:  $(BUILDDIR)/% | $(BUILDDIR)
	gzip < $< > $@

$(BUILDDIR)/timesignal.1.html: $(BUILDDIR)/timesignal.1 | $(BUILDDIR)
	mandoc -T html -O style=timesignal.css $< > $@
	sed -i -z 's|<!--.*-->\n||' $@
	sed -i '2i <!-- SPDX-License-Identifier: GPL-3.0-or-later -->\
<!--\
\
  timesignal.1.html: Man page rendered to HTML.\
\
  This file is part of timesignal.\
\
  Copyright © 2025 James Seo <james@equiv.tech>\
\
-->' $@
	sed -i -z 's|\(<b>timesignal</b>\)|<a href="$(URL)">\1</a>|g' $@
	sed -i -z 's|\(<b>timesignal.conf</b>(5)\)|<a href="timesignal.conf.5.html">\1</a>|g' $@
	sed -i -z 's|\(<b>BPC</b>\)|<a href="https://en.wikipedia.org/wiki/BPC_(time_signal)">\1</a>|' $@
	sed -i -z 's|\(<b>DCF77</b>\)|<a href="https://en.wikipedia.org/wiki/DCF77">\1</a>|' $@
	sed -i -z 's|\(<b>JJY</b>\)|<a href="https://en.wikipedia.org/wiki/JJY">\1</a>|' $@
	sed -i -z 's|\(<b>MSF</b>\)|<a href="https://en.wikipedia.org/wiki/Time_from_NPL_(MSF)">\1</a>|' $@
	sed -i -z 's|\(<b>WWVB</b>\)|<a href="https://en.wikipedia.org/wiki/WWVB">\1</a>|' $@
	sed -i -z 's|\(<i>STATION</i>\))|<a href="#Time_station_selection">\1</a>)|' $@
	sed -i -z 's|\(<b>-a</b>/<b>--audible</b>\)|<a href="#a">\1</a>|' $@
	sed -i -z 's|\(<b>-b</b>/<b>--base</b>\)|<a href="#b">\1</a>|' $@
	sed -i -z 's|\(<b>-m</b>/<b>--method</b>\)|<a href="#m">\1</a>|' $@
	sed -i -z 's|\(<b>-r</b>/<b>--rate</b>\)|<a href="#r">\1</a>|' $@
	sed -i -z 's|\(<b>-S</b>/<b>--smooth</b>\)|<a href="#S">\1</a>|' $@
	sed -i -z 's|<br/>\n<pre>|<pre>|g' $@
	sed -i -z 's|</pre>\n<br/>|</pre>|g' $@

$(BUILDDIR)/timesignal.conf.5.html: $(BUILDDIR)/timesignal.conf.5 | $(BUILDDIR)
	mandoc -T html -O style=timesignal.css $< > $@
	sed -i -z 's|<!--.*-->\n||' $@
	sed -i '2i <!-- SPDX-License-Identifier: GPL-3.0-or-later -->\
<!--\
\
  timesignal.conf.5.html: Man page for timesignal.conf rendered to HTML.\
\
  This file is part of timesignal.\
\
  Copyright © 2025 James Seo <james@equiv.tech>\
\
-->' $@
	sed -i -z 's|\(<b>timesignal</b>(1)\)|<a href="timesignal.1.html">\1</a>|g' $@
	sed -i -z 's|\(<b>-C</b>/<b>--config</b>\)|<a href="timesignal.1.html#C">\1</a>|' $@

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
