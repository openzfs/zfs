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

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/grow_replicas/grow_replicas.cfg
. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

ismounted $TESTFS && \
        log_must $ZFS umount $TESTDIR
destroy_pool "$TESTPOOL"

#
# Here we create & destroy a zpool using the disks
# because this resets the partitions to normal
#
if [[ -z $DISK ]]; then
        create_pool "ZZZ" "$DISK0 $DISK1"
        destroy_pool "ZZZ"
else
        create_pool "ZZZ" "$DISK"
        destroy_pool "ZZZ"
fi

log_pass
