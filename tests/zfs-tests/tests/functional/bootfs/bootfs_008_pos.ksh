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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# setting bootfs on a dataset which has gzip compression enabled will not fail
#
# STRATEGY:
# 1. create pools based on a valid vdev
# 2. create a filesystem on this pool and set the compression property to
#    gzip1-9
# 3. set the pool's bootfs property to filesystem we just configured which
#    should not fail
#

verify_runnable "global"

function cleanup {
	if poolexists $TESTPOOL ; then
		destroy_pool "$TESTPOOL"
	fi

	if [[ -f $VDEV ]]; then
		log_must rm -f $VDEV
	fi
}

typeset assert_msg="setting bootfs on a dataset which has gzip \
    compression enabled will not fail"

typeset VDEV=$TEST_BASE_DIR/bootfs_008_pos_a.$$.dat
typeset COMP_FS=$TESTPOOL/COMP_FS

log_onexit cleanup
log_assert $assert_msg

log_must mkfile $MINVDEVSIZE $VDEV
log_must zpool create $TESTPOOL $VDEV
log_must zfs create $COMP_FS

typeset -i i=0
set -A gtype "gzip" "gzip-1" "gzip-2" "gzip-3" "gzip-4" "gzip-5" \
	     "gzip-6" "gzip-7" "gzip-8" "gzip-9"

while (( i < ${#gtype[@]} )); do
	log_must zfs set compression=${gtype[i]} $COMP_FS
	log_must zpool set bootfs=$COMP_FS $TESTPOOL
	log_must zfs set compression=off $COMP_FS
	(( i += 1 ))
done

log_pass $assert_msg
