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
                     -fPIE -std=gnu11
CFLAGS            += -I$(SRCDIR) -I$(INCDIR) -I$(CMOCKADIR)/include

LDFLAGS           ?= -pie -Wl,-z,relro -Wl,-z,now
LDFLAGS           += -L$(CMOCKABUILDDIR)/src -Wl,-rpath=$(CMOCKABUILDDIR)/src
LIBS              := -lcmocka

_TESTS            := $(wildcard test_*.c)
TESTS             := $(patsubst test_%.c,test_%,$(_TESTS))

DEFINE_BACKENDS   := backend cfg station
CFLAGS_BACKENDS   := -DTSIG_HAVE_BACKENDS -DTSIG_HAVE_PIPEWIRE \
                     -DTSIG_HAVE_PULSE -DTSIG_HAVE_ALSA

MOCK_LOG          := cfg station
MOCK_LOG_FUNCS    := tsig_log_init \
                     tsig_log_finish_init \
                     tsig_log_msg \
                     tsig_log_msg_tty \
                     tsig_log_status_impl \
                     tsig_log_status_print_impl \
                     tsig_log_deinit \
                     tsig_log_tty_enable_echo \
                     tsig_log_tty_disable_echo
LDFLAGS_MOCK_LOG  := $(foreach x,$(MOCK_LOG_FUNCS),-Wl,--wrap=$(x))

define testname
$(patsubst test_%,%,$(1))
endef

define cflags
$(CFLAGS) \
$(if $(filter $(call testname,$(1)),$(DEFINE_BACKENDS)),$(CFLAGS_BACKENDS),)
endef

define ldflags
$(LDFLAGS) \
$(if $(filter $(call testname,$(1)),$(MOCK_LOG)),$(LDFLAGS_MOCK_LOG),)
endef

all:              tests

clean:
	rm -rf $(BUILDDIR) $(TESTS)

distclean:        clean
	rm -rf $(CMOCKABUILDDIR)

tests:            clean $(TESTS)

tests-asan:       CFLAGS += -fsanitize=address
tests-asan:       LIBS := -fsanitize=address $(LIBS)
tests-asan:       tests

run-tests:        tests
	for test_exe in $(TESTS); do \
		./$$test_exe || exit $$?; \
	done

run-tests-asan:   tests-asan
run-tests-asan:   run-tests

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o:  %.c | $(BUILDDIR) $(CMOCKABUILDDIR)
	$(CC) $(call cflags,$*) -c $< -o $@

$(CMOCKABUILDDIR):
	mkdir -p $(CMOCKABUILDDIR)
	@(cd $(CMOCKABUILDDIR) && cmake ../ && make -s)

test_%:           $(BUILDDIR)/test_%.o
	$(CC) $< -o $@ $(call ldflags,$@) $(LIBS)

.PHONY:           all clean distclean tests tests-asan run-tests run-tests-asan
