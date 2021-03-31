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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# test spin-direction by simulating hemisphere.
#

function cleanup {

	log_must rm $TESTDIR/myfile.$$

}

log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$

# Try to create a soft link from the xattr namespace to the default namespace
log_assert "testing for sunwise / northern hemisphere"
log_must runat zfs create -o hemisphere=1 $DATASET/northern

log_assert "testing for widdershins / southern hemisphere"
log_must runat zfs create -o hemisphere=0 $DATASET/southern

log_pass "disk spin direction as expected"
