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
