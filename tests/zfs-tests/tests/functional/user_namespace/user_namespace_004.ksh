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

. $STF_SUITE/tests/functional/user_namespace/user_namespace_common.kshlib

#
# DESCRIPTION:
#	Regression test for safeguards around the delegation of datasets to
#	user namespaces.
#
# STRATEGY:
#       1. Check that 'zfs zone' correctly handles the case of the first
#	   argument being a non-namespace file.
#       2. Check that 'zfs zone' correctly handles the case of the first
#	   argument being a non-namespace and non-existent file.
#

verify_runnable "both"

user_ns_cleanup() {
	if [ -n "$temp_file" ]; then
		log_must rm -f "$temp_file"
	fi

	log_must zfs destroy -r "$TESTPOOL/userns"
}

log_assert "Check zfs zone command handling of non-namespace files"

# Pass if user namespaces are not supported.
unshare -Urm echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi

log_onexit user_ns_cleanup

# Create the baseline datasets.
log_must zfs create -o zoned=on "$TESTPOOL/userns"

# 1. Try to pass a non-namespace file to zfs zone.
temp_file="$(mktemp)"
log_mustnot zfs zone "$temp_file" "$TESTPOOL/userns"

# 2. Try to pass a non-namespace and non-existent file to zfs zone.
log_mustnot zfs zone "$TEMP_BASE_DIR/nonexistent" "$TESTPOOL/userns"

log_pass "Check zfs zone command handling of non-namespace files"
