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
TESTDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zpool_import

PROGS = cleanup \
	setup \
	zpool_import_001_pos \
	zpool_import_002_pos \
	zpool_import_003_pos \
	zpool_import_004_pos \
	zpool_import_005_pos \
	zpool_import_006_pos \
	zpool_import_007_pos \
	zpool_import_008_pos \
	zpool_import_009_neg \
	zpool_import_010_pos \
	zpool_import_011_neg \
	zpool_import_012_pos \
	zpool_import_013_neg \
	zpool_import_all_001_pos \
	zpool_import_features_001_pos \
	zpool_import_features_002_neg \
	zpool_import_features_003_pos \
	zpool_import_missing_001_pos \
	zpool_import_missing_002_pos \
	zpool_import_missing_003_pos \
	zpool_import_rename_001_pos

FILES = zpool_import.cfg

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
