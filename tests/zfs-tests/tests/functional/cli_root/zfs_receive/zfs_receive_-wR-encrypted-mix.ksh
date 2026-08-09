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
# Copyright (c) 2022 by Attila Fülöp <attila@fueloep.org>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#      ZFS should receive a raw send of a mix of unencrypted and encrypted
#      child datasets
#
#      The layout of the datasets is:  enc/unenc/enc/unenc
#
# STRATEGY:
# 1. Create the dataset hierarchy
# 2. Snapshot the dataset hierarchy
# 3. Send -Rw the dataset hierarchy and receive into a top-level dataset
# 4. Check the encryption property of the received datasets

verify_runnable "both"

function cleanup
{
	datasetexists "$TESTPOOL/$TESTFS1" && \
		destroy_dataset "$TESTPOOL/$TESTFS1" -r

	datasetexists "$TESTPOOL/$TESTFS2" && \
		destroy_dataset "$TESTPOOL/$TESTFS2" -r
}

log_onexit cleanup

log_assert "ZFS should receive a mix of un/encrypted childs"

typeset src="$TESTPOOL/$TESTFS1"
typeset dst="$TESTPOOL/$TESTFS2"
typeset snap="snap"

echo "password" | \
	create_dataset "$src" -o encryption=on -o keyformat=passphrase
create_dataset "$src/u" "-o encryption=off"
echo "password" | \
	create_dataset "$src/u/e" -o encryption=on -o keyformat=passphrase
create_dataset "$src/u/e/u" -o encryption=off

log_must zfs snapshot -r "$src@$snap"
log_must eval "zfs send -Rw $src@$snap | zfs receive -u $dst"
log_must test "$(get_prop 'encryption' $dst)" != "off"
log_must test "$(get_prop 'encryption' $dst/u)" == "off"
log_must test "$(get_prop 'encryption' $dst/u/e)" != "off"
log_must test "$(get_prop 'encryption' $dst/u/e/u)" == "off"

log_pass "ZFS can receive a mix of un/encrypted childs"
