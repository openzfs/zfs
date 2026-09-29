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
# Copyright (c) 2026 by iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs create -V' honours a refreservation given with the 'refreserv' alias.
#
# STRATEGY:
# 1. Create volumes with -o refreserv=none, a size, and auto.
# 2. Verify that each gets the refreservation that the same value given
#    with -o refreservation= gives.
#

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $vol $vol_ref; do
		datasetexists $ds && destroy_dataset $ds
	done
}

log_onexit cleanup

log_assert "'zfs create -V' honours the refreserv alias"

vol=$TESTPOOL/$TESTVOL-alias
vol_ref=$TESTPOOL/$TESTVOL-full

for value in none 10M auto; do
	log_must zfs create -V 64M -o refreserv=$value $vol
	log_must zfs create -V 64M -o refreservation=$value $vol_ref
	log_must test $(get_prop refreservation $vol) -eq \
	    $(get_prop refreservation $vol_ref)
	destroy_dataset $vol
	destroy_dataset $vol_ref
done

log_pass "'zfs create -V' honours the refreserv alias"
