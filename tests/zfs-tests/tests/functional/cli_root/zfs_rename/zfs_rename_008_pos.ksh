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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs rename -r can rename snapshot recursively.
#
# STRATEGY:
#	1. Create snapshot recursively.
#	2. Rename snapshot recursively.
#	3. Verify rename -r snapshot correctly.
#

verify_runnable "both"

function cleanup
{
	typeset -i i=0
	while ((i < ${#datasets[@]})); do
		datasetexists ${datasets[$i]}@snap && \
			destroy_dataset ${datasets[$i]}@snap

		datasetexists ${datasets[$i]}@snap-new && \
			destroy_dataset ${datasets[$i]}@snap-new

		((i += 1))
	done
}

log_assert "zfs rename -r can rename snapshot recursively."
log_onexit cleanup

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

log_must zfs snapshot -r ${TESTPOOL}@snap
typeset -i i=0
while ((i < ${#datasets[@]})); do
	log_must datasetexists ${datasets[$i]}@snap

	((i += 1))
done

log_must zfs rename -r ${TESTPOOL}@snap ${TESTPOOL}@snap-new
i=0
while ((i < ${#datasets[@]})); do
	log_must datasetexists ${datasets[$i]}@snap-new

	((i += 1))
done

log_must zfs destroy -rf ${TESTPOOL}@snap-new

log_pass "Verify zfs rename -r passed."
