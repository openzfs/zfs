#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/slop_space/slop_space.cfg

#
# DESCRIPTION:
# Kernel module tunable 'spa_slop_shift' should accept only expected values
#
# STRATEGY:
# 1. Verify valid 'spa_slop_shift' values can be set
# 2. Verify invalid tunable values cannot be set

verify_runnable "both"

function cleanup
{
	log_must set_tunable32 "spa_slop_shift" "$SPA_SLOP_SHIFT_DEFAULT"
}

log_assert "Kernel module tunable 'spa_slop_shift' accepts only expected values"
log_onexit cleanup

SPA_SLOP_SHIFT_DEFAULT=$(get_tunable spa_slop_shift)
typeset -a badvalues=("text" "0x" "0xff" "-10" "-")

# 1. Verify valid "spa_slop_shift" values can be set
for value in `seq $SPA_SLOP_SHIFT_MIN $SPA_SLOP_SHIFT_MAX`; do
	log_must set_tunable32 "spa_slop_shift" "$value"
done

# 2. Verify invalid tunable values cannot be set
log_mustnot set_tunable32 "spa_slop_shift" "$((SPA_SLOP_SHIFT_MIN - 1))"
log_mustnot set_tunable32 "spa_slop_shift" "$((SPA_SLOP_SHIFT_MAX + 1))"
for value in ${badvalues[@]}; do
	log_mustnot set_tunable32 "spa_slop_shift" "$value"
done

log_pass "Kernel module tunable 'spa_slop_shift' accepts only expected values"
