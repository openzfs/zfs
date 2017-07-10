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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# Description:
# Once set zpool autoexpand=off, zpool can *NOT* autoexpand by
# Dynamic LUN Expansion
#
#
# STRATEGY:
# 1) Create a pool
# 2) Create volumes on top of the pool
# 3) Create pool by using the zvols and set autoexpand=off
# 4) Expand the vol size by zfs set volsize
# 5) Check that the pool size is not changed
#

verify_runnable "global"

function cleanup
{
        if poolexists $TESTPOOL1; then
                log_must $ZPOOL destroy $TESTPOOL1
        fi

	for i in 1 2 3; do
		if datasetexists $VFS/vol$i; then
			log_must $ZFS destroy $VFS/vol$i
		fi
	done
}

log_onexit cleanup

log_assert "zpool can not expand if set autoexpand=off after LUN expansion"

for i  in 1 2 3; do
	log_must $ZFS create -V $org_size $VFS/vol$i
done

for type in " " mirror raidz raidz2; do
	log_must $ZPOOL create $TESTPOOL1 $type ${ZVOL_DEVDIR}/$VFS/vol1 \
	    ${ZVOL_DEVDIR}/$VFS/vol2 ${ZVOL_DEVDIR}/$VFS/vol3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)
	if [[ $autoexp != "off" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should off but is " \
		    "$autoexp"
	fi

	typeset prev_size=$(get_pool_prop size $TESTPOOL1)

	for i in 1 2 3; do
		log_must $ZFS set volsize=$exp_size $VFS/vol$i
	done

	$SYNC
	$SLEEP 10
	$SYNC

	# check for zpool history for the pool size expansion
	$ZPOOL history -il $TESTPOOL1 | $GREP "pool '$TESTPOOL1' size:" | \
	    $GREP "vdev online" >/dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		log_fail "pool $TESTPOOL1 is not autoexpand after LUN " \
		    "expansion"
	fi

	typeset expand_size=$(get_pool_prop size $TESTPOOL1)

	if [[ "$prev_size" != "$expand_size" ]]; then
		log_fail "pool $TESTPOOL1 size changed after LUN expansion"
	fi

	log_must $ZPOOL destroy $TESTPOOL1

	for i in 1 2 3; do
		log_must $ZFS set volsize=$org_size $VFS/vol$i
	done

done

log_pass "zpool can not expand if set autoexpand=off after LUN expansion"
