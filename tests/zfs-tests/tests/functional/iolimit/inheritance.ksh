#! /bin/ksh -p
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
# Copyright (c) 2024 The FreeBSD Foundation
#
# This software was developed by Pawel Dawidek <pawel@dawidek.net>
# under sponsorship from the FreeBSD Foundation.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/iolimit/iolimit_common.kshlib

verify_runnable "both"

log_assert "Verify that limits are properly inherited"

iolimit_reset

log_must create_dataset "$TESTPOOL/$TESTFS/mother"
log_must create_dataset "$TESTPOOL/$TESTFS/father"

log_must truncate -s 1G "$TESTDIR/mother/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/file" "/dev/null"
log_must truncate -s 1G "$TESTDIR/father/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/father/file" "/dev/null"

if false; then
# Parent configuration exists before child creation.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/file" "/dev/null"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Parent configuration is done after child creation.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/file" "/dev/null"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Child is moved between a parent with no configuration to a parent with limits and back.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/file" "/dev/null"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/father"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 5 5 "$TESTDIR/father/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/father"

# Child is moved between a parent with limits to a parent with no limits and back.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 5 1 "$TESTDIR/father/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Child is moved between parent with different limits and back.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/father"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must iolimit_bw 1 8 8 "$TESTDIR/mother/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 8 4 "$TESTDIR/father/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 8 8 "$TESTDIR/mother/child/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/father"

# Repeat the tests above, but test grandchild, so its direct ancestor doesn't have limits.

# Parent configuration exists before child and grandchild creation.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/file" "/dev/null"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Parent configuration is done after child and grandchild creation.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Child is moved between a parent with no configuration to a parent with limits and back.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/father"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 5 5 "$TESTDIR/father/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 5 1 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/father"

# Child is moved between a parent with limits to a parent with no limits and back.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 5 1 "$TESTDIR/father/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 5 5 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"

# Child is moved between parent with different limits and back.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/father"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 8 8 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/father/child"
log_must iolimit_bw 1 8 4 "$TESTDIR/father/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/father/child" "$TESTPOOL/$TESTFS/mother/child"
log_must iolimit_bw 1 8 8 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must destroy_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/father"
fi

# Revert the order of datasets when all datasets have limits.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 12 3 "$TESTDIR/mother/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/child/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child/grandchild" "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother" "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the bottom doesn't affect ancestors.
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the top does affect descendants.
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 12 2 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 12 2 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 2 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/grandchild/child/mother" "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild/child"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild"
#
# Revert the order of limits.
#
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 10 10 "$TESTDIR/mother/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/mother/child/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child/grandchild" "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 12 3 "$TESTDIR/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother" "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the bottom doesn't affect ancestors.
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 12 3 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the top does affect descendants.
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/grandchild/child/mother" "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild/child"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild"

# Revert the order datasets when grandchild doesn't have limits.
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 12 3 "$TESTDIR/mother/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child/grandchild" "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 10 1 "$TESTDIR/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother" "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the bottom doesn't affect ancestors.
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 10 1 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the top does affect descendants.
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 10 10 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/grandchild/child/mother" "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild/child"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild"
#
# Revert the order of limits.
#
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child"
log_must create_dataset "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/mother/child"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother/child/grandchild"
log_must truncate -s 1G "$TESTDIR/mother/child/file"
log_must truncate -s 1G "$TESTDIR/mother/child/grandchild/file"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/child/file" "/dev/null"
log_must iolimit_bw 1 12 6 "$TESTDIR/mother/child/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child/grandchild" "$TESTPOOL/$TESTFS/grandchild"
log_must iolimit_bw 1 10 1 "$TESTDIR/grandchild/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother/child" "$TESTPOOL/$TESTFS/grandchild/child"
log_must iolimit_bw 1 12 3 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/mother" "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 12 6 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
# Changing limits at the bottom doesn't affect ancestors.
log_must zfs set iolimit_bw_read=6M "$TESTPOOL/$TESTFS/grandchild/child/mother"
log_must iolimit_bw 1 10 1 "$TESTDIR/grandchild/file" "/dev/null"
log_must iolimit_bw 1 12 3 "$TESTDIR/grandchild/child/file" "/dev/null"
log_must iolimit_bw 1 12 3 "$TESTDIR/grandchild/child/mother/file" "/dev/null"
log_must zfs rename "$TESTPOOL/$TESTFS/grandchild/child/mother" "$TESTPOOL/$TESTFS/mother"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/mother"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild/child"
log_must destroy_dataset "$TESTPOOL/$TESTFS/grandchild"

log_must destroy_dataset "$TESTPOOL/$TESTFS/mother"
log_must destroy_dataset "$TESTPOOL/$TESTFS/father"

log_pass
