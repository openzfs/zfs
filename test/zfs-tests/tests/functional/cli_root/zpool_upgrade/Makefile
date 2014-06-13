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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

include $(SRC)/Makefile.master

ROOTOPTPKG = $(ROOT)/opt/zfs-tests
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zpool_upgrade

PROGS = cleanup \
	setup \
	zpool_upgrade_001_pos \
	zpool_upgrade_002_pos \
	zpool_upgrade_003_pos \
	zpool_upgrade_004_pos \
	zpool_upgrade_005_neg \
	zpool_upgrade_006_neg \
	zpool_upgrade_007_pos \
	zpool_upgrade_008_pos \
	zpool_upgrade_009_neg

FILES = zpool_upgrade.cfg \
	zpool_upgrade.kshlib

CMDS = $(PROGS:%=$(TESTDIR)/%) $(FILES:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(TESTDIR)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

$(TESTDIR)/%: %.ksh
	$(INS.rename)

SUBDIRS = blockfiles

include $(SRC)/test/Makefile.com
