#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	Changes made by 'zfs inherit' can be properly received.
#
# STRATEGY:
#	1. Inherit property for filesystem and volume
#	2. Send and restore them in the target pool
#	3. Verify all the datasets can be properly backup and receive
#

verify_runnable "both"

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Verify changes made by 'zfs inherit' can be properly received."
log_onexit cleanup

#
# Setting all the $FS properties as local value,
#
for prop in $(fs_inherit_prop); do
	value=$(get_prop $prop $POOL/$FS)
	log_must zfs set $prop=$value $POOL/$FS
done

#
# Inherit properties in sub-datasets
#
for ds in "$POOL/$FS/fs1" "$POOL/$FS/fs1/fs2" "$POOL/$FS/fs1/fclone"; do
	for prop in $(fs_inherit_prop); do
		log_must zfs inherit $prop $ds
	done
done
if is_global_zone; then
	for prop in $(vol_inherit_prop); do
		log_must zfs inherit $prop $POOL/$FS/vol
	done
fi

#
# Verify datasets can be backup and restore correctly
# Unmount $POOL/$FS to avoid two fs mount in the same mountpoint
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-R"
log_must zfs unmount -f $POOL/$FS
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-R"

dstds=$(get_dst_ds $POOL $POOL2)
#
# Define all the POOL/POOL2 datasets pair
#
set -A pair 	"$POOL" 		"$dstds" 		\
		"$POOL/$FS" 		"$dstds/$FS" 		\
		"$POOL/$FS/fs1"		"$dstds/$FS/fs1"	\
		"$POOL/$FS/fs1/fs2"	"$dstds/$FS/fs1/fs2"	\
		"$POOL/pclone"		"$dstds/pclone"		\
		"$POOL/$FS/fs1/fclone"	"$dstds/$FS/fs1/fclone"

if is_global_zone ; then
	typeset -i n=${#pair[@]}
	pair[((n))]="$POOL/vol"; 	pair[((n+1))]="$dstds/vol"
	pair[((n+2))]="$POOL/$FS/vol"	pair[((n+3))]="$dstds/$FS/vol"
fi

#
# Verify all the sub-datasets can be properly received.
#
log_must cmp_ds_subs $POOL $dstds
typeset -i i=0
while ((i < ${#pair[@]})); do
	log_must cmp_ds_prop ${pair[$i]} ${pair[((i+1))]}
	((i += 2))
done

log_pass "Changes made by 'zfs inherit' can be properly received."
