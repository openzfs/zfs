#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#
#
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A clone takes the source's DVAs whole, so copies of a block
#	stored with different copies= counts cannot share it.  An
#	older copy with two DVAs must not keep the copies with one
#	from pairing with each other, and no block may lose a DVA.
#
# STRATEGY:
#	1. Write a file into a copies=2 dataset and run clonedup, so
#	   the next run matches against it as older data.
#	2. Copy the file twice into a copies=1 dataset and once into
#	   the copies=2 dataset.
#	3. Run clonedup.
#	4. Check that the two copies=1 copies share every block, that
#	   the copies=2 copy shares the older file's blocks, and that
#	   every block kept its DVA count.
#	5. Check that no pair was declined for unequal DVA counts.
#

verify_runnable "global"

# The DVA count of each of a file's blocks, one per line.
function cd_ndvas # ds file
{
	zdb -dddddd -vvvvv $1 -O $2 | awk '/ L0 / { n = 0
	    for (i = 1; i <= NF; i++)
		if ($i ~ /^<?[0-9]+:[0-9a-f]+:[0-9a-f]+G?>?$/)
			n++
	    print n }'
}

function cd_check_ndvas # ds file want
{
	typeset got=$(cd_ndvas $1 $2 | sort -u)

	log_note "$1/$2: DVA counts '$got', want $3"
	log_must [ "$got" = "$3" ]
}

log_assert "copies with unequal DVA counts pair among themselves"
log_onexit clonedup_cleanup
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
log_must zfs create -o copies=2 $TESTPOOL/two
log_must zfs create -o copies=1 $TESTPOOL/one
clonedup_write /$TESTPOOL/two/old
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

clonedup_dup /$TESTPOOL/two/old /$TESTPOOL/one/a
clonedup_dup /$TESTPOOL/two/old /$TESTPOOL/one/b
clonedup_dup /$TESTPOOL/two/old /$TESTPOOL/two/c
clonedup_sync
cd_check_ndvas $TESTPOOL/two old 2
cd_check_ndvas $TESTPOOL/one a 1

clonedup_run
clonedup_sync
clonedup_check_shared $TESTPOOL/one a $TESTPOOL/one b "$all"
clonedup_check_shared $TESTPOOL/two old $TESTPOOL/two c "$all"
clonedup_check_shared $TESTPOOL/two old $TESTPOOL/one a ""
cd_check_ndvas $TESTPOOL/one a 1
cd_check_ndvas $TESTPOOL/one b 1
cd_check_ndvas $TESTPOOL/two old 2
cd_check_ndvas $TESTPOOL/two c 2
typeset f
for f in one/a one/b two/c; do
	log_must cmp /$TESTPOOL/two/old /$TESTPOOL/$f
done
clonedup_leakcheck

log_pass "copies with unequal DVA counts pair among themselves"
