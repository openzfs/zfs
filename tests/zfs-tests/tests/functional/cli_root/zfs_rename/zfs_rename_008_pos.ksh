#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
