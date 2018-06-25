#!/bin/ksh -p

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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Ensure that we can expand a device while the pool has a
#	checkpoint but in the case of a rewind that device rewinds
#	back to its previous size.
#
# STRATEGY:
#	1. Create pool
#	2. Populate it
#	3. Take checkpoint
#	4. Expand the device and modify some data
#	   (include at least one destructive change)
#	5. Rewind to checkpoint
#	6. Verify that we rewinded successfully and check if the
#	   device shows up expanded in the vdev list
#

verify_runnable "global"

EXPSZ=2G

setup_nested_pools
log_onexit cleanup_nested_pools

populate_nested_pool
INITSZ=$(zpool list -v | grep "$FILEDISK1" | awk '{print $2}')
log_must zpool checkpoint $NESTEDPOOL

log_must truncate -s $EXPSZ $FILEDISK1
log_must zpool online -e $NESTEDPOOL $FILEDISK1
NEWSZ=$(zpool list -v | grep "$FILEDISK1" | awk '{print $2}')
nested_change_state_after_checkpoint
log_mustnot [ "$INITSZ" = "$NEWSZ" ]

log_must zpool export $NESTEDPOOL
log_must zpool import -d $FILEDISKDIR --rewind-to-checkpoint $NESTEDPOOL

nested_verify_pre_checkpoint_state
FINSZ=$(zpool list -v | grep "$FILEDISK1" | awk '{print $2}')
log_must [ "$INITSZ" = "$FINSZ" ]

log_pass "LUN expansion rewinded correctly."
