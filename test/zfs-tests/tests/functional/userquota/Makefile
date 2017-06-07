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
TESTDIR = $(ROOTOPTPKG)/tests/functional/userquota

PROGS = cleanup \
	groupspace_001_pos \
	groupspace_002_pos \
	setup \
	userquota_001_pos \
	userquota_002_pos \
	userquota_003_pos \
	userquota_004_pos \
	userquota_005_neg \
	userquota_006_pos \
	userquota_007_pos \
	userquota_008_pos \
	userquota_009_pos \
	userquota_010_pos \
	userquota_011_pos \
	userquota_012_neg \
	userspace_001_pos \
	userspace_002_pos

FILES = userquota.cfg \
	userquota_common.kshlib

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
