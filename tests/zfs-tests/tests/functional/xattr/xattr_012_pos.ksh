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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# xattr file sizes count towards normal disk usage
#
# STRATEGY:
#	1. Create a file, and check pool and filesystem usage
#       2. Create a 200mb xattr in that file
#	3. Check pool and filesystem usage, to ensure it reflects the size
#	   of the xattr
#

function cleanup {
	log_must rm $TESTDIR/myfile.$$
	if is_freebsd; then
		log_must rm /tmp/xattr.$$
	fi
}

function get_pool_size {
	poolname=$1
	psize=$(zpool list -H -o allocated $poolname)
	if [[ $psize == *[mM] ]]
	then
		returnvalue=$(echo $psize | sed -e 's/m//g' -e 's/M//g')
		returnvalue=$((returnvalue * 1024))
	else
		returnvalue=$(echo $psize | sed -e 's/k//g' -e 's/K//g')
	fi
	echo $returnvalue
}

log_assert "xattr file sizes count towards normal disk usage"
log_onexit cleanup

log_must touch $TESTDIR/myfile.$$

POOL_SIZE=0
NEW_POOL_SIZE=0

if is_global_zone
then
	# get pool and filesystem sizes. Since we're starting with an empty
	# pool, the usage should be small - a few k.
	POOL_SIZE=$(get_pool_size $TESTPOOL)
fi

FS_SIZE=$(zfs get -p -H -o value used $TESTPOOL/$TESTFS)

if is_freebsd; then
	# FreeBSD setextattr has awful scaling with respect to input size.
	# It reallocs after every 1024 bytes. For now we'll just break up
	# the 200MB into 10 20MB attributes, but this test could be revisited
	# if someone cared about large extattrs and improves setextattr -i.
	log_must mkfile 20m /tmp/xattr.$$
	for i in {0..10}; do
		log_must eval "set_xattr_stdin xattr$i $TESTDIR/myfile.$$ \
		    < /tmp/xattr.$$"
	done
elif is_linux; then
	# Linux setxattr() syscalls limits individual xattrs to 64k.  Create
	# 100 files, with 128 xattrs each of size 16k.  100*128*16k=200m
	log_must xattrtest -k -f 100 -x 128 -s 16384 -p $TESTDIR
else
	log_must runat $TESTDIR/myfile.$$ mkfile 200m xattr
fi

#Make sure the newly created file is counted into zpool usage
sync_pool

# now check to see if our pool disk usage has increased
if is_global_zone
then
	NEW_POOL_SIZE=$(get_pool_size $TESTPOOL)
	(($NEW_POOL_SIZE <= $POOL_SIZE)) && \
	    log_fail "The new pool size $NEW_POOL_SIZE was less \
            than or equal to the old pool size $POOL_SIZE."

fi

# also make sure our filesystem usage has increased
NEW_FS_SIZE=$(zfs get -p -H -o value used $TESTPOOL/$TESTFS)
(($NEW_FS_SIZE <= $FS_SIZE)) && \
    log_fail "The new filesystem size $NEW_FS_SIZE was less \
    than or equal to the old filesystem size $FS_SIZE."

log_pass "xattr file sizes count towards normal disk usage"
