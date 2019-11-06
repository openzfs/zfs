#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0 OR MPL-2.0

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
# Copyright (C) 2019 Pavel Snajdr <snajpa@snajpa.net
# Copyright (C) 2019 vpsFree.cz
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	log_must umount -f $TESTDIR/overlay
	log_must rm -rf $TESTDIR/*
}

log_assert "ZFS supports being upper/lower/mnt of overlayfs."
log_onexit cleanup

modprobe overlay 2> /dev/null
cd $TESTDIR
mkdir lower upper work overlay
touch lower/{a,b}
touch upper/{c,d}
echo "orig" > lower/testfile
echo "upper" > upper/testfile

# Basic overlayfs mount
log_must mount -t overlay \
    -o lowerdir=lower/,upperdir=upper/,workdir=work/ \
    none overlay/

# Does presented overlay have all the files we expect?
log_must stat overlay/{a,b,c,d,testfile}

# We'd expect content of the upper test file
log_must grep upper overlay/testfile

echo "new" > overlay/testfile

# We'd expect content of the upper test file changed now
log_must grep new upper/testfile

log_must umount $TESTDIR/overlay

log_assert "ZFS supports being upper/lower/mnt of overlayfs as expected."
