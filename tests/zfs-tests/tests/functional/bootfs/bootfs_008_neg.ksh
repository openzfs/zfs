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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# setting bootfs on a dataset which has gzip compression enabled will fail
#
# STRATEGY:
# 1. create pools based on a valid vdev
# 2. create a filesytem on this pool and set the compression property to gzip1-9
# 3. set the pool's bootfs property to filesystem we just configured which
#    should fail
#

verify_runnable "global"

function cleanup {
	if poolexists $TESTPOOL ; then
		destroy_pool "$TESTPOOL"
	fi

	if [[ -f $VDEV ]]; then
		log_must $RM -f $VDEV
	fi
}

typeset assert_msg="setting bootfs on a dataset which has gzip \
    compression enabled will fail"

typeset VDEV=$TESTDIR/bootfs_008_neg_a.$$.dat
typeset COMP_FS=$TESTPOOL/COMP_FS

log_onexit cleanup
log_assert $assert_msg

log_must $MKFILE 300m $VDEV
log_must $ZPOOL create $TESTPOOL $VDEV
log_must $ZFS create $COMP_FS

typeset -i i=0
set -A gtype "gzip" "gzip-1" "gzip-2" "gzip-3" "gzip-4" "gzip-5" \
	     "gzip-6" "gzip-7" "gzip-8" "gzip-9"

while (( i < ${#gtype[@]} )); do
	log_must $ZFS set compression=${gtype[i]} $COMP_FS
	log_mustnot $ZPOOL set bootfs=$COMP_FS $TESTPOOL
	log_must $ZFS set compression=off $COMP_FS
	(( i += 1 ))
done

log_pass $assert_msg
