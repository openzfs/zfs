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
temp_file="$(TMPDIR=$TEST_BASE_DIR mktemp)"
log_mustnot zfs zone "$temp_file" "$TESTPOOL/userns"

# 2. Try to pass a non-namespace and non-existent file to zfs zone.
log_mustnot zfs zone "$TEMP_BASE_DIR/nonexistent" "$TESTPOOL/userns"

log_pass "Check zfs zone command handling of non-namespace files"
