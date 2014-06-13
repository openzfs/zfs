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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that the ZFS mdb dcmds and walkers are working as expected.
#
# STRATEGY:
#	1) Given a list of dcmds and walkers
#	2) Step through each element of the list
#	3) Verify the output by checking for "mdb:" in the output string
#

function cleanup
{
	$RM -f $OUTFILE
}

verify_runnable "global"
log_onexit cleanup

OUTFILE='/var/tmp/mdb-outfile'
set -A dcmds "::walk spa" \
	"::walk spa | ::spa " \
	"::walk spa | ::spa -c" \
	"::walk spa | ::spa -v" \
	"::walk spa | ::spa_config" \
	"::walk spa | ::spa_space" \
	"::walk spa | ::spa_space -b" \
	"::walk spa | ::spa_vdevs" \
	"::walk spa | ::walk metaslab" \
	"::walk spa | ::print struct spa spa_root_vdev | ::vdev" \
	"::walk spa | ::print struct spa spa_root_vdev | ::vdev -re" \
	"::dbufs" \
	"::dbufs -n mos -o mdn -l 0 -b 0" \
	"::dbufs | ::dbuf" \
	"::dbuf_stats" \
	"::abuf_find 1 2" \
        "::walk spa | ::print -a struct spa spa_uberblock.ub_rootbp | ::blkptr" \
        "::walk spa | ::print -a struct spa spa_dsl_pool->dp_dirty_datasets | ::walk txg_list" \
        "::walk spa | ::walk zms_freelist"
#
# The commands above were supplied by the ZFS development team. The idea is to
# do as much checking as possible without the need to hardcode addresses.
#

log_assert "Verify that the ZFS mdb dcmds and walkers are working as expected."

typeset -i RET=0

i=0
while (( $i < ${#dcmds[*]} )); do
	log_note "Verifying: '${dcmds[i]}'"
        $ECHO "${dcmds[i]}" | $MDB -k > $OUTFILE 2>&1
	RET=$?
	if (( $RET != 0 )); then
		log_fail "mdb '${dcmds[i]}' returned error $RET"
	fi

	#
	# mdb prefixes all errors with "mdb: " so we check the output.
	#
	$GREP "mdb:" $OUTFILE > /dev/null 2>&1
	RET=$?
	if (( $RET == 0 )); then
		$ECHO "mdb '${dcmds[i]}' contained 'mdb:'"
		# Using $TAIL limits the number of lines in the log
		$TAIL -100 $OUTFILE
		log_fail "mdb walker or dcmd failed"
	fi

        ((i = i + 1))
done

log_pass "The ZFS mdb dcmds and walkers are working as expected."
