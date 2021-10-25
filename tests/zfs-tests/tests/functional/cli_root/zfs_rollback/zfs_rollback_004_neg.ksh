#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#
# DESCRIPTION:
#	'zfs rollback' should fail when passing invalid options, too many
#	arguments,non-snapshot datasets or missing datasets
#
# STRATEGY:
#	1. Create an array of invalid options
#	2. Execute 'zfs rollback' with invalid options, too many arguments
#	   or missing datasets
#	3. Verify 'zfs rollback' return with errors
#

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
		snapexists ${ds}@$TESTSNAP && \
			destroy_dataset ${ds}@$TESTSNAP
	done
}

log_assert "'zfs rollback' should fail with bad options,too many arguments," \
	"non-snapshot datasets or missing datasets."
log_onexit cleanup

set -A badopts "r" "R" "f" "-F" "-rF" "-RF" "-fF" "-?" "-*" "-blah" "-1" "-2"

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
	log_must zfs snapshot ${ds}@$TESTSNAP
done

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
	for opt in "" "-r" "-R" "-f" "-rR" "-rf" "-rRf"; do
		log_mustnot eval "zfs rollback $opt $ds >/dev/null 2>&1"
		log_mustnot eval "zfs rollback $opt ${ds}@$TESTSNAP \
			${ds}@$TESTSNAP >/dev/null 2>&1"
		log_mustnot eval "zfs rollback $opt >/dev/null 2>&1"
		# zfs rollback should fail with non-existen snapshot
		log_mustnot eval "zfs rollback $opt ${ds}@nosnap >/dev/null 2>&1"
	done

	for badopt in ${badopts[@]}; do
		log_mustnot eval "zfs rollback $badopt ${ds}@$TESTSNAP \
				>/dev/null 2>&1"
	done
done

log_pass "'zfs rollback' fails as expected with illegal arguments."
