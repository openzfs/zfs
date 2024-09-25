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
# Verify 'zpool reguid' can change pool's GUID.
#
# STRATEGY:
# 1. Use zpool get to obtain the initial GUID of a pool.
# 2. Change pool's GUID with zpool reguid.
# 3. Verify the GUID has changed to a random GUID.
#
# 4. Change pool's GUID with zpool reguid -g.
# 5. Verify the GUID has changed to the specified GUID.
#

# set_guid guid [expected_guid]
set_guid() {
	gflag_guid="$1"
	expected_guid="${2:-"$gflag_guid"}"

	initial_guid="$(zpool get -H -o value guid "$TESTPOOL")"
	log_assert "Verify 'zpool reguid -g \"$gflag_guid\"' sets GUID as expected."
	log_must zpool reguid -g "$gflag_guid" "$TESTPOOL"
	retrieved_guid="$(zpool get -H -o value guid "$TESTPOOL")"
	if [[ "$retrieved_guid" == "" ]]; then
		log_fail "Unable to obtain the new GUID of pool $TESTPOOL"
	fi
	if [[ "$expected_guid" != "$retrieved_guid" ]]; then
		log_fail "GUID set to '$retrieved_guid' instead of '$expected_guid'"
	fi
}

log_assert "Verify 'zpool reguid' picks a new random GUID for the pool."
initial_guid="$(zpool get -H -o value guid "$TESTPOOL")"
if [[ $initial_guid == "" ]]; then
	log_fail "Unable to obtain the initial GUID of pool $TESTPOOL"
fi
log_must zpool reguid "$TESTPOOL"
new_guid="$(zpool get -H -o value guid "$TESTPOOL")"
if [[ "$new_guid" == "" ]]; then
	log_fail "Unable to obtain the new GUID of pool $TESTPOOL"
fi
if [[ "$initial_guid" == "$new_guid" ]]; then
	log_fail "GUID change failed; GUID has not changed: $initial_guid"
fi

for g in "$(echo '2^64 - 1' | bc)" "314"; do
	set_guid "$g"
done
# zpool-reguid(8) will strip the leading 0.
set_guid 0123 "123"
# GUID "-1" is effectively 2^64 - 1 in value.
set_guid -1 "$(echo '2^64 - 1' | bc)"

log_pass "'zpool reguid' changes GUID as expected."
