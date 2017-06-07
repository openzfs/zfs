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
BLOCKFILESDIR = $(ROOTOPTPKG)/tests/functional/cli_root/zpool_upgrade/blockfiles

FILES = zfs-broken-mirror1.dat.bz2 \
	zfs-broken-mirror2.dat.bz2 \
	zfs-pool-v1.dat.bz2 \
	zfs-pool-v10.dat.bz2 \
	zfs-pool-v11.dat.bz2 \
	zfs-pool-v12.dat.bz2 \
	zfs-pool-v13.dat.bz2 \
	zfs-pool-v14.dat.bz2 \
	zfs-pool-v15.dat.bz2 \
	zfs-pool-v1mirror1.dat.bz2 \
	zfs-pool-v1mirror2.dat.bz2 \
	zfs-pool-v1mirror3.dat.bz2 \
	zfs-pool-v1raidz1.dat.bz2 \
	zfs-pool-v1raidz2.dat.bz2 \
	zfs-pool-v1raidz3.dat.bz2 \
	zfs-pool-v1stripe1.dat.bz2 \
	zfs-pool-v1stripe2.dat.bz2 \
	zfs-pool-v1stripe3.dat.bz2 \
	zfs-pool-v2.dat.bz2 \
	zfs-pool-v2mirror1.dat.bz2 \
	zfs-pool-v2mirror2.dat.bz2 \
	zfs-pool-v2mirror3.dat.bz2 \
	zfs-pool-v2raidz1.dat.bz2 \
	zfs-pool-v2raidz2.dat.bz2 \
	zfs-pool-v2raidz3.dat.bz2 \
	zfs-pool-v2stripe1.dat.bz2 \
	zfs-pool-v2stripe2.dat.bz2 \
	zfs-pool-v2stripe3.dat.bz2 \
	zfs-pool-v3.dat.bz2 \
	zfs-pool-v3hotspare1.dat.bz2 \
	zfs-pool-v3hotspare2.dat.bz2 \
	zfs-pool-v3hotspare3.dat.bz2 \
	zfs-pool-v3mirror1.dat.bz2 \
	zfs-pool-v3mirror2.dat.bz2 \
	zfs-pool-v3mirror3.dat.bz2 \
	zfs-pool-v3raidz1.dat.bz2 \
	zfs-pool-v3raidz2.dat.bz2 \
	zfs-pool-v3raidz21.dat.bz2 \
	zfs-pool-v3raidz22.dat.bz2 \
	zfs-pool-v3raidz23.dat.bz2 \
	zfs-pool-v3raidz3.dat.bz2 \
	zfs-pool-v3stripe1.dat.bz2 \
	zfs-pool-v3stripe2.dat.bz2 \
	zfs-pool-v3stripe3.dat.bz2 \
	zfs-pool-v4.dat.bz2 \
	zfs-pool-v5.dat.bz2 \
	zfs-pool-v6.dat.bz2 \
	zfs-pool-v7.dat.bz2 \
	zfs-pool-v8.dat.bz2 \
	zfs-pool-v9.dat.bz2 \
	zfs-pool-v999.dat.bz2 \
	zfs-pool-vBROKEN.dat.bz2

CMDS = $(FILES:%=$(BLOCKFILESDIR)/%)
$(CMDS) := FILEMODE = 0444

all lint clean clobber:

install: $(CMDS)

$(CMDS): $(BLOCKFILESDIR)

$(BLOCKFILESDIR):
	$(INS.dir)

$(BLOCKFILESDIR)/%: %
	$(INS.file)
