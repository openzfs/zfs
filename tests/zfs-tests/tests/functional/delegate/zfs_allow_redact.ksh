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

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
# Verify the 'redact' permission is required, and is enough, to run
# 'zfs redact'.
#
# STRATEGY:
# 1. Verify an unprivileged user cannot redact, even with other permissions
#    used by redacted sends.
# 2. Verify the user can redact once granted 'redact' on the redacted dataset
#    and on the dataset of the redaction snapshot.
# 3. Verify a local-only grant is enough on the redacted dataset.
# 4. Verify 'redact' on the redacted dataset alone, or on the dataset of the
#    redaction snapshot alone, is not enough.
#

verify_runnable "both"

log_assert "Verify the 'redact' permission allows 'zfs redact'."
log_onexit restore_root_datasets

for dtst in $DATASETS; do
	log_must restore_root_datasets
	log_must verify_noperm $dtst "redact" $STAFF1

	log_must zfs allow $STAFF1 "send,snapshot,bookmark,clone,create,mount" \
	    $dtst
	log_must verify_noperm $dtst "redact" $STAFF1

	log_must restore_root_datasets
	log_must zfs allow $STAFF1 "redact" $dtst
	log_must eval "zfs allow $dtst | grep -q redact"
	log_must verify_perm $dtst "redact" $STAFF1

	log_must zfs unallow $STAFF1 "redact" $dtst
	log_must verify_noperm $dtst "redact" $STAFF1

	# A local grant covers the redacted snapshot of the dataset itself.
	log_must restore_root_datasets
	log_must zfs allow -l $STAFF1 "redact" $dtst
	log_must verify_perm $dtst "redact" $STAFF1
done

log_must restore_root_datasets

log_pass "Verify the 'redact' permission allows 'zfs redact'."
