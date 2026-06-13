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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

verify_runnable "both"

log_assert "Verify send sub-permissions correctly limit send options."
log_onexit restore_root_datasets

for dtst in $DATASETS; do

	for enc in '' 'y' ; do
		# full 'send' permission can do it all
		log_must restore_root_datasets $enc
		log_must zfs allow $STAFF1 "send" $dtst
		log_must verify_perm $dtst "send" $STAFF1
		log_must verify_perm $dtst "send_raw" $STAFF1

		# 'send:raw' can only do raw send
		log_must restore_root_datasets $enc
		log_must zfs allow $STAFF1 "send:raw" $dtst
		log_must verify_noperm $dtst "send" $STAFF1
		log_must verify_perm $dtst "send_raw" $STAFF1
	done

	# 'send:encrypted' cannot do anything on unencrypted datasets
	log_must restore_root_datasets
	log_must zfs allow $STAFF1 "send:encrypted" $dtst
	log_must verify_noperm $dtst "send" $STAFF1
	log_must verify_noperm $dtst "send_raw" $STAFF1

	# 'send:encrypted' can do raw send only on encrypted datasets
	log_must restore_root_datasets 'y'
	log_must zfs allow $STAFF1 "send:encrypted" $dtst
	log_must verify_noperm $dtst "send" $STAFF1
	log_must verify_perm $dtst "send_raw" $STAFF1

done

log_must restore_root_datasets

log_pass "Verify send-permissions correctly limit send options."
