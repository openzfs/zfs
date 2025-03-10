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
