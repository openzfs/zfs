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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_user/zfs_list/zfs_list.kshlib

#
# DESCRIPTION:
# The sort functionality in 'zfs list' works as expected.
#
# STRATEGY:
# 1. Using several zfs datasets with names, creation dates, checksum options
# 2. Sort the datasets by name, checksum options, creation date.
# 3. Verify that the datasets are sorted correctly.
#

verify_runnable "both"

# datasets ordered by name
fs_name="Apple Banana Carrot Orange apple banana carrot"
vol_name="Apple-vol Banana-vol Carrot-vol Orange-vol apple-vol"
vol_name="$vol_name banana-vol carrot-vol"
if is_global_zone ; then
	snap_name="Apple-vol@snap Apple@snap Banana-vol@snap Banana@snap"
	snap_name="$snap_name Carrot-vol@snap Carrot@snap Orange-vol@snap Orange@snap"
	snap_name="$snap_name apple-vol@snap apple@snap banana-vol@snap banana@snap"
	snap_name="$snap_name carrot-vol@snap carrot@snap"
else
	snap_name="Apple@snap Banana@snap"
	snap_name="$snap_name Carrot@snap Orange@snap"
	snap_name="$snap_name apple@snap banana@snap"
	snap_name="$snap_name carrot@snap"
fi

fs_creation=$fs_name
vol_creation=$vol_name
if is_global_zone ; then
	snap_creation="Apple@snap Apple-vol@snap Banana@snap Banana-vol@snap"
	snap_creation="$snap_creation Carrot@snap Carrot-vol@snap Orange@snap Orange-vol@snap"
	snap_creation="$snap_creation apple@snap apple-vol@snap banana@snap banana-vol@snap"
	snap_creation="$snap_creation carrot@snap carrot-vol@snap"
else
	snap_creation="Apple@snap Banana@snap"
	snap_creation="$snap_creation Carrot@snap Orange@snap"
	snap_creation="$snap_creation apple@snap banana@snap"
	snap_creation="$snap_creation carrot@snap"
fi

#
# datasets ordered by checksum options (note, Orange, Carrot & Banana have the
# same checksum options, so ZFS should revert to sorting them alphabetically by
# name)
#
fs_cksum="carrot apple banana Apple Banana Carrot Orange"
vol_cksum="carrot-vol apple-vol banana-vol Apple-vol Banana-vol"
vol_cksum="$vol_cksum Carrot-vol Orange-vol"
snap_cksum=$snap_creation

fs_rev_cksum="carrot apple banana Apple Orange Carrot Banana"
vol_rev_cksum="carrot-vol apple-vol banana-vol Apple-vol Orange-vol"
vol_rev_cksum="$vol_rev_cksum Carrot-vol Banana-vol"

log_assert "The sort functionality in 'zfs list' works as expected."

#
# we must be in the C locale here, as running in other locales
# will make zfs use that locale's sort order.
#
LC_ALL=C; export LC_ALL

# sort by creation
verify_sort \
	"zfs list -H -r -o name -s creation -t filesystem $TESTPOOL/$TESTFS" \
	"$fs_creation" "creation date"
if is_global_zone ; then
	verify_sort \
	"zfs list -H -r -o name -s creation -t volume $TESTPOOL/$TESTFS" \
	"$vol_creation" "creation date"
fi
verify_sort \
	"zfs list -H -r -o name -s creation -t snapshot $TESTPOOL/$TESTFS" \
	"$snap_creation" "creation date"

# sort by checksum
verify_sort \
	"zfs list -H -r -o name -s checksum -t filesystem $TESTPOOL/$TESTFS" \
	"$fs_cksum" "checksum"
if is_global_zone ; then
	verify_sort \
	"zfs list -H -r -o name -s checksum -t volume $TESTPOOL/$TESTFS" \
	"$vol_cksum" "checksum"
fi
verify_sort \
	"zfs list -H -r -o name -s checksum -t snapshot $TESTPOOL/$TESTFS" \
	"$snap_cksum" "checksum"
verify_sort \
	"zfs list -H -r -o name -S checksum -t snapshot $TESTPOOL/$TESTFS" \
	"$snap_cksum" "checksum"

# sort by name
verify_sort \
	"zfs list -H -r -o name -s name -t filesystem $TESTPOOL/$TESTFS" \
	"$fs_name" "name"
if is_global_zone ; then
	verify_sort \
	"zfs list -H -r -o name -s name -t volume $TESTPOOL/$TESTFS" \
	"$vol_name" "name"
fi
verify_sort \
	"zfs list -H -r -o name -s name -t snapshot $TESTPOOL/$TESTFS" \
	"$snap_name" "name"

# reverse sort by creation
verify_reverse_sort \
	"zfs list -H -r -o name -S creation -t filesystem $TESTPOOL/$TESTFS" \
	"$fs_creation" "creation date"
if is_global_zone ; then
	verify_reverse_sort \
	"zfs list -H -r -o name -S creation -t volume $TESTPOOL/$TESTFS" \
	"$vol_creation" "creation date"
fi
verify_reverse_sort \
	"zfs list -H -r -o name -S creation -t snapshot $TESTPOOL/$TESTFS" \
	"$snap_creation" "creation date"

# reverse sort by checksum
verify_reverse_sort \
	"zfs list -H -r -o name -S checksum -t filesystem $TESTPOOL/$TESTFS" \
	"$fs_rev_cksum" "checksum"
if is_global_zone ; then
	verify_reverse_sort \
	"zfs list -H -r -o name -S checksum -t volume $TESTPOOL/$TESTFS" \
	"$vol_rev_cksum" "checksum"
fi

# reverse sort by name
verify_reverse_sort \
	"zfs list -H -r -o name -S name -t filesystem $TESTPOOL/$TESTFS"\
	"$fs_name" "name"
if is_global_zone ; then
	verify_reverse_sort \
	"zfs list -H -r -o name -S name -t volume $TESTPOOL/$TESTFS"\
	"$vol_name" "name"
fi
verify_reverse_sort \
	"zfs list -H -r -o name -S name -t snapshot $TESTPOOL/$TESTFS"\
	"$snap_name" "name"

log_pass "The sort functionality in 'zfs list' works as expected."
