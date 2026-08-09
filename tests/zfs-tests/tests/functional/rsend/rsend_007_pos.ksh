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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	Rename parent filesystem name will not change the dependent order.
#
# STRATEGY:
#	1. Separately rename pool clone, filesystem and volume name.
#	2. Send -R all the POOL
#	3. Verify renamed dataset will not change the snapshot dependent order.
#

verify_runnable "both"

set -A 	dtst \
	"$POOL/pclone"		"$POOL/$FS/pclone"	\
	"$POOL/$FS/fs1/fs2"	"$POOL/fs2"
if is_global_zone ; then
	typeset -i n=${#dtst[@]}
	dtst[((n))]="$POOL/vol"; 	dtst[((n+1))]="$POOL/$FS/fs1/vol"
fi

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Rename parent filesystem name will not change the dependent order."
log_onexit cleanup

typeset -i i=0
while ((i < ${#dtst[@]})); do
	log_must zfs rename ${dtst[$i]} ${dtst[((i+1))]}

	((i += 2))
done

#
# Verify zfs send -R should succeed
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"
dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds

#
# Verify zfs send -R -I should succeed
#
log_must eval "zfs send -R -I @init $dstds@final > " \
        "$BACKDIR/pool-init-final-IR"
list=$(getds_with_suffix $dstds @snapA)
list="$list $(getds_with_suffix $dstds @snapB)"
list="$list $(getds_with_suffix $dstds @snapC)"
list="$list $(getds_with_suffix $dstds @final)"
log_must destroy_tree $list
if is_global_zone ; then
	log_must eval "zfs receive -d -F $dstds < $BACKDIR/pool-init-final-IR"
else
	zfs receive -d -F $dstds < $BACKDIR/pool-init-final-IR
fi
log_must cmp_ds_subs $POOL $dstds

log_pass "Rename parent filesystem name will not change the dependent order."
