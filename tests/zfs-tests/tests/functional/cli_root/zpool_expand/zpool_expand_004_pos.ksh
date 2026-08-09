#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2015 by Delphix. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# After vdev expansion, all 4 labels have the same set of uberblocks.
#
#
# STRATEGY:
# 1) Create 3 files
# 2) Create a pool backed by the files
# 3) Expand the files' size with truncate
# 4) Use zpool online -e to expand the vdevs
# 5) Check that for all the devices, all 4 labels have the same uberblocks
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	for i in 1 2 3; do
		[ -e ${TEMPFILE}.$i ] && log_must rm ${TEMPFILE}.$i
	done
}

log_onexit cleanup

log_assert "After vdev expansion, all 4 labels have the same set of uberblocks."

for type in " " mirror raidz draid; do
	for i in 1 2 3; do
		log_must truncate -s $org_size ${TEMPFILE}.$i
	done

	log_must zpool create $TESTPOOL1 $type $TEMPFILE.1 \
	    $TEMPFILE.2 $TEMPFILE.3

	sync_pool $TESTPOOL1

	for i in 1 2 3; do
		log_must truncate -s $exp_size ${TEMPFILE}.$i
	done

	for i in 1 2 3; do
		log_must zpool online -e $TESTPOOL1 ${TEMPFILE}.$i
	done

	sync_pool $TESTPOOL1


	for i in 1 2 3; do
		non_uniform=$(zdb -lu ${TEMPFILE}.$i	| \
		    grep 'labels = '			| \
		    grep -c -v 'labels = 0 1 2 3')

		log_note "non-uniform label count: $non_uniform"

		if [[ $non_uniform -ne 0 ]]; then
			log_fail "After vdev expansion, all labels contents are not identical"
		fi
	done

	log_must zpool destroy $TESTPOOL1
done

log_pass "After vdev expansion, all 4 labels have the same set of uberblocks."
