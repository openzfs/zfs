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
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The 'defaultvolblocksize' filesystem property supplies the volblocksize
# for volumes created with 'zfs create -V' when no explicit -o volblocksize
# is given, and is inherited by descendant filesystems.
#
# STRATEGY:
# 1. Set defaultvolblocksize on a filesystem and create a child filesystem.
# 2. A volume created under the child with no -o volblocksize inherits it.
# 3. An explicit -o volblocksize still overrides the default.
# 4. A volume outside the subtree falls back to the built-in default.
# 5. 'none' reverts to the built-in default.
# 6. Invalid (non power-of-2) values are rejected.
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/dvb && destroy_dataset $TESTPOOL/dvb -r
	datasetexists $TESTPOOL/vol_out && destroy_dataset $TESTPOOL/vol_out -f
}

log_onexit cleanup

log_assert "defaultvolblocksize supplies the default volblocksize for" \
    "volumes created without an explicit -o volblocksize."

typeset defsize=16384

# Default is 'none' (human-readable) / 0 (parseable).
log_must zfs create $TESTPOOL/dvb
log_must eval \
    "[[ $(zfs get -H -o value defaultvolblocksize $TESTPOOL/dvb) == none ]]"
log_must eval "[[ $(get_prop defaultvolblocksize $TESTPOOL/dvb) == 0 ]]"

# Set it, and confirm inheritance one level down.
log_must zfs set defaultvolblocksize=64k $TESTPOOL/dvb
log_must zfs create $TESTPOOL/dvb/child
log_must eval \
    "[[ $(get_prop defaultvolblocksize $TESTPOOL/dvb/child) == 65536 ]]"

# A volume with no -o volblocksize inherits the default.  Sparse (-s) volumes
# avoid a refreservation so the test does not depend on pool size.
log_must zfs create -s -V 100m $TESTPOOL/dvb/child/vol
log_must eval "[[ $(get_prop volblocksize $TESTPOOL/dvb/child/vol) == 65536 ]]"

# An explicit -o volblocksize wins over the inherited default.
log_must zfs create -s -V 100m -o volblocksize=8k $TESTPOOL/dvb/child/vol2
log_must eval "[[ $(get_prop volblocksize $TESTPOOL/dvb/child/vol2) == 8192 ]]"

# A volume outside the subtree falls back to the built-in default.
log_must zfs create -s -V 100m $TESTPOOL/vol_out
log_must eval \
    "[[ $(get_prop volblocksize $TESTPOOL/vol_out) == $defsize ]]"

# "zfs create -p" resolves inheritance once ancestors exist (kernel path).
log_must zfs set defaultvolblocksize=32k $TESTPOOL/dvb
log_must zfs create -s -V 100m -p $TESTPOOL/dvb/a/b/vol_p
log_must eval "[[ $(get_prop volblocksize $TESTPOOL/dvb/a/b/vol_p) == 32768 ]]"

# Reverting to 'none' restores the built-in default for new volumes.
log_must zfs set defaultvolblocksize=none $TESTPOOL/dvb
log_must zfs create -s -V 100m $TESTPOOL/dvb/vol_none
log_must eval \
    "[[ $(get_prop volblocksize $TESTPOOL/dvb/vol_none) == $defsize ]]"

# Invalid (non power-of-2) values are rejected.
log_mustnot zfs set defaultvolblocksize=3000 $TESTPOOL/dvb
log_mustnot zfs set defaultvolblocksize=7000 $TESTPOOL/dvb

log_pass "defaultvolblocksize behaves as expected."
