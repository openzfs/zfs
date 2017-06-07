#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

include $(SRC)/Makefile.master

ROOTOPTPKG = $(ROOT)/opt/zfs-tests
TESTDIR = $(ROOTOPTPKG)/tests/functional/slog

PROGS = cleanup \
	setup \
	slog_001_pos \
	slog_002_pos \
	slog_003_pos \
	slog_004_pos \
	slog_005_pos \
	slog_006_pos \
	slog_007_pos \
	slog_008_neg \
	slog_009_neg \
	slog_010_neg \
	slog_011_neg \
	slog_012_neg \
	slog_013_pos \
	slog_014_pos

FILES = slog.cfg \
	slog.kshlib

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %.ksh
	$(INS.rename)

$(TESTDIR)/%: %
	$(INS.file)
