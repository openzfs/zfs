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
TESTDIR = $(ROOTOPTPKG)/tests/functional/inheritance

PROGS = cleanup \
	inherit_001_pos

FILES = config001.cfg \
	config002.cfg \
	config003.cfg \
	config004.cfg \
	config005.cfg \
	config006.cfg \
	config007.cfg \
	config008.cfg \
	config009.cfg \
	config010.cfg \
	config011.cfg \
	config012.cfg \
	config013.cfg \
	config014.cfg \
	config015.cfg \
	config016.cfg \
	config017.cfg \
	config018.cfg \
	config019.cfg \
	config020.cfg \
	config021.cfg \
	config022.cfg \
	config023.cfg \
	config024.cfg \
	inherit.kshlib \
	state001.cfg \
	state002.cfg \
	state003.cfg \
	state004.cfg \
	state005.cfg \
	state006.cfg \
	state007.cfg \
	state008.cfg \
	state009.cfg \
	state010.cfg \
	state011.cfg \
	state012.cfg \
	state013.cfg \
	state014.cfg \
	state015.cfg \
	state016.cfg \
	state017.cfg \
	state018.cfg \
	state019.cfg \
	state020.cfg \
	state021.cfg \
	state022.cfg \
	state023.cfg \
	state024.cfg

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
