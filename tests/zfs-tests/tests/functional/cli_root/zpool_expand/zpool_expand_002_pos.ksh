#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# After zpool online -e poolname zvol vdevs, zpool can autoexpand by
# Dynamic LUN Expansion
#
#
# STRATEGY:
# 1) Create 3 files
# 2) Create a pool backed by the files
# 3) Expand the files' size with truncate
# 4) Use zpool online -e to online the vdevs
# 5) Check that the pool size was expanded
#

verify_runnable "global"

function cleanup
{
        if poolexists $TESTPOOL1; then
                log_must zpool destroy $TESTPOOL1
        fi

	for i in 1 2 3; do
		[ -e ${TEMPFILE}.$i ] && log_must rm ${TEMPFILE}.$i
	done
}

log_onexit cleanup

log_assert "zpool can expand after zpool online -e zvol vdevs on LUN expansion"


for type in " " mirror raidz raidz2; do
	for i in 1 2 3; do
		log_must truncate -s $org_size ${TEMPFILE}.$i
	done

	log_must zpool create $TESTPOOL1 $type $TEMPFILE.1 \
	    $TEMPFILE.2 $TEMPFILE.3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)

	if [[ $autoexp != "off" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should off but is " \
		    "$autoexp"
	fi
	typeset prev_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_prev_size=$(zfs get -p avail $TESTPOOL1 | tail -1 | \
	    awk '{print $3}')

	for i in 1 2 3; do
		log_must truncate -s $exp_size ${TEMPFILE}.$i
	done

	for i in 1 2 3; do
		log_must zpool online -e $TESTPOOL1 ${TEMPFILE}.$i
	done

	sync
	sleep 10
	sync

	typeset expand_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_expand_size=$(zfs get -p avail $TESTPOOL1 | tail -1 | \
	    awk '{print $3}')
	log_note "$TESTPOOL1 $type has previous size: $prev_size and " \
	    "expanded size: $expand_size"

	# compare available pool size from zfs
	if [[ $zfs_expand_size -gt $zfs_prev_size ]]; then
	# check for zpool history for the pool size expansion
		if [[ $type == " " ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			typeset	size_addition=$(zpool history -il $TESTPOOL1 \
			    | grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size}" | wc -l)

			if [[ $size_addition -ne $i ]]; then
				log_fail "pool $TESTPOOL1 is not autoexpand " \
				    "after LUN expansion"
			fi
		elif [[ $type == "mirror" ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size})" >/dev/null 2>&1

			if [[ $? -ne 0 ]]; then
				log_fail "pool $TESTPOOL1 is not autoexpand " \
				    "after LUN expansion"
			fi
		else
			typeset expansion_size=$((3*($exp_size-$org_size)))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size})" >/dev/null 2>&1

			if [[ $? -ne 0 ]] ; then
				log_fail "pool $TESTPOOL1 is not autoexpand " \
				    "after LUN expansion"
			fi
		fi
	else
		log_fail "pool $TESTPOOL1 is not autoexpanded after LUN " \
		    "expansion"
	fi
	log_must zpool destroy $TESTPOOL1
done
log_pass "zpool can expand after zpool online -e zvol vdevs on LUN expansion"
