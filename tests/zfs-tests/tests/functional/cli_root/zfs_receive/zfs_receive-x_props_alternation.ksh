#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

#
# Copyright (c) 2026 by . All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Verify 'zfs receive -x' consistently excludes a property across
# incremental sends with -p, and does not alternate.
#
# STRATEGY:
# 1. Create source and destination datasets.
# 2. Set a native property (refquota) on source.
# 3. Full send with -p, receive with -x refquota.
# 4. Verify refquota is not set on destination.
# 5. Loop several incremental sends/receives with -p/-x.
# 6. Each iteration verify refquota stays consistently excluded.
#

verify_runnable "global"

function cleanup
{
	destroy_dataset "$orig" "-rf"
	destroy_dataset "$dest" "-rf"
}

log_assert "'zfs receive -x' consistently excludes properties" \
    "across incremental sends."
log_onexit cleanup

orig=$TESTPOOL/$TESTFS1
dest=$TESTPOOL/$TESTFS2

# setup source and destination
log_must zfs create $orig
log_must zfs create $dest

# set a native property on source
log_must zfs set refquota=64M $orig
log_must check_prop_source $orig refquota 67108864 local

# full send with -p, receive with -x
log_must zfs snapshot $orig@snap1
log_must eval "zfs send -p $orig@snap1 | zfs receive -e -x refquota $dest"

# destination should NOT have refquota from the stream
log_must [ "$(get_prop refquota $dest/$TESTFS1)" = "0" ]

# incremental sends: the excluded property must never appear on dest
typeset -i i=2
while (( i <= 4 )); do
	log_must zfs snapshot $orig@snap$i
	log_must eval "zfs send -p -i $orig@snap$((i-1)) $orig@snap$i \
	    | zfs receive -e -F -x refquota $dest"
	log_must [ "$(get_prop refquota $dest/$TESTFS1)" = "0" ]
	(( i = i + 1 ))
done

log_pass "'zfs receive -x' consistently excludes properties" \
    "across incremental sends."
