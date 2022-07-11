#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

if is_linux || is_freebsd; then
	log_unsupported "Privilege tests require pfexec command"
fi

verify_runnable "global"

ZFS_USER=$(<$TEST_BASE_DIR/zfs-privs-test-user.txt)
[[ -z $ZFS_USER ]] && log_fail "no ZFS_USER found"

USES_NIS=$(<$TEST_BASE_DIR/zfs-privs-test-nis.txt)

if [ "${USES_NIS}" == "true" ]
then
    svcadm enable svc:/network/nis/client:default
fi

userdel $ZFS_USER
[[ -d /export/home/$ZFS_USER ]] && rm -rf /export/home/$ZFS_USER
rm $TEST_BASE_DIR/zfs-privs-test-nis.txt
rm $TEST_BASE_DIR/zfs-privs-test-user.txt

default_cleanup
