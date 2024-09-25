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
# Copyright 2023 Mateusz Piotrowski
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zpool reguid' does not accept invalid GUIDs.
#
# STRATEGY:
# 1. Call zpool reguid with an invalid GUID.
# 2. Verify that the call fails.
# 3. Verify that the pool GUID did not change.
#
# 4. Call zpool reguid with a GUID that is already in use.
# 5. Verify that the call fails.
#

check_guid() {
	invalid_guid="$1"
	initial_guid="$(zpool get -H -o value guid "$TESTPOOL")"
	log_assert "'zpool reguid' will not accept invalid GUID '$invalid_guid'"
	if zpool reguid -g "$invalid_guid" "$TESTPOOL"; then
		log_fail "'zpool reguid' accepted invalid GUID: $invalid_guid"
	fi
	final_guid="$(zpool get -H -o value guid "$TESTPOOL")"
	if [[ "$initial_guid" != "$final_guid" ]]; then
		log_fail "Invalid GUID change from '$initial_guid' to '$final_guid'"
	fi
}

log_assert "Verify 'zpool reguid' does not accept invalid GUIDs"

for ig in "$(echo '2^64' | bc)" 0xA 0xa 0; do
	check_guid "$ig"
done

guid="42"
log_assert "Verify 'zpool reguid -g' does not accept GUID which are already in use"
log_must zpool reguid -g "$guid" "$TESTPOOL"
if zpool reguid -g "$guid" "$TESTPOOL"; then
	log_fail "'zpool reguid' accepted GUID that was already in use: $invalid_guid"
fi

log_pass "'zpool reguid' does not accept invalid GUIDs."
