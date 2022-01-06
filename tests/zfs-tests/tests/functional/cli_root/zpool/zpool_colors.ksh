#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Test that zpool status colored output works.
#
# STRATEGY:
# 1. Create a pool with a bunch of errors and force fault one of the vdevs.
# 2. Look for 'pool:' in bold.
# 3. Look for 'DEGRADED' in yellow
# 3. Look for 'FAULTED' in red
#

verify_runnable "both"

function cleanup
{
	zinject -c all
}

log_onexit cleanup

log_assert "Test colorized zpool status output"

DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_must dd if=/dev/urandom of=/$TESTDIR/testfile bs=10M count=1

sync_all_pools

log_must zpool offline -f $TESTPOOL $DISK3
log_must wait_for_degraded $TESTPOOL
log_must zinject -d $DISK2 -e io -T read -f 20 $TESTPOOL
log_must zinject -d $DISK2 -e io -T write -f 20 $TESTPOOL


log_must zpool scrub -w $TESTPOOL
log_must zinject -c all


# Use 'script' to fake zpool status into thinking it's running in a tty.
# Log the output here in case it's needed for postmortem.
log_note "$(faketty TERM=xterm-256color ZFS_COLOR=1 zpool status)"

# Replace the escape codes with "ESC" so they're easier to grep
out="$(faketty TERM=xterm-256color ZFS_COLOR=1 zpool status | \
    grep -E 'pool:|DEGRADED' | \
    sed -r 's/[[:space:]]+//g;'$(echo -e 's/\033/ESC/g'))"

log_note "$(echo $out)"

log_note "Look for 'pool:' in bold"
log_must eval "echo \"$out\" | grep -q 'ESC\[1mpool:ESC\[0m' "

log_note "Look for 'DEGRADED' in yellow"
log_must eval "echo \"$out\" | grep -q 'ESC\[0;33mDEGRADEDESC\[0m'"

#
# The escape code for 'FAULTED' is a little more tricky.  The line starts like
# this:
#
# <start red escape code> loop2  FAULTED <end escape code>
#
# Luckily, awk counts the start and end escape codes as separate fields, so
# we can easily remove the vdev field to get what we want.
#
out="$(faketty TERM=xterm-256color ZFS_COLOR=1 zpool status \
    | awk '/FAULTED/{print $1$3$4}' | sed -r $(echo -e 's/\033/ESC/g'))"

log_note "Look for 'FAULTED' in red"
log_must eval "echo \"$out\" | grep -q 'ESC\[0;31mFAULTEDESC\[0m'"

log_pass "zpool status displayed colors"
