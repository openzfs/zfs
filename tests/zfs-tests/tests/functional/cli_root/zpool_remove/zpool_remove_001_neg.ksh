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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_remove/zpool_remove.cfg

#
# DESCRIPTION:
# Verify that 'zpool can not remove device except inactive hot spares from pool'
#
# STRATEGY:
# 1. Create all kinds of pool (strip, mirror, raidz, hotspare)
# 2. Try to remove device from the pool
# 3. Verify that the remove failed.
#

typeset vdev_devs="${DISK0}"
typeset mirror_devs="${DISK0} ${DISK1}"
typeset raidz_devs=${mirror_devs}
typeset raidz1_devs=${mirror_devs}
typeset raidz2_devs="${mirror_devs} ${DISK2}"
typeset spare_devs1="${DISK0}"
typeset spare_devs2="${DISK1}"

function check_remove
{
        typeset pool=$1
        typeset devs="$2"
        typeset dev

        for dev in $devs; do
                log_mustnot zpool remove $dev
        done

        destroy_pool $pool

}

function cleanup
{
        if poolexists $TESTPOOL; then
                destroy_pool $TESTPOOL
        fi
}

set -A create_args "$vdev_devs" "mirror $mirror_devs"  \
		"raidz $raidz_devs" "raidz $raidz1_devs" \
		"raidz2 $raidz2_devs" \
		"$spare_devs1 spare $spare_devs2"

set -A verify_disks "$vdev_devs" "$mirror_devs" "$raidz_devs" \
		"$raidz1_devs" "$raidz2_devs" "$spare_devs1"


log_assert "Check zpool remove <pool> <device> can not remove " \
	"active device from pool"

log_onexit cleanup

typeset -i i=0
while [[ $i -lt ${#create_args[*]} ]]; do
	log_must zpool create $TESTPOOL ${create_args[i]}
	check_remove $TESTPOOL "${verify_disks[i]}"
	(( i = i + 1))
done

log_pass "'zpool remove <pool> <device> fail as expected .'"
