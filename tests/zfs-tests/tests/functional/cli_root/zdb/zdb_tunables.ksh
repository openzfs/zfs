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
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

log_assert "zdb can work with libzpool tunables"

# a tunable name by itself, or with the "show" command, produces name and value
log_must eval 'zdb -o zfs_recover | grep -qE "^zfs_recover: 0$"'
log_must eval 'zdb -o show=zfs_recover | grep -qE "^zfs_recover: 0$"'

# info about a tunable shows a different format
log_must eval 'zdb -o info=zfs_recover | grep -qE "^zfs_recover \[[[:alnum:]_]+ r[dw]]: .+"'

# "show" by itself shows all the tunables and their values
# this tests limits to 50 tunables, and then counts the number that match
# the format, which should be all of them
log_must test $(zdb -o show | head -50 | grep -cE "^[[:alnum:]_]+: .+") -eq 50

# "info" by itself shows info about all tunables
# like previous test, we limit and then count
log_must test $(zdb -o info | head -50 | grep -cE "^[[:alnum:]_]+ \[[[:alnum:]_]+ r[dw]]: .+") -eq 50

# can't lookup nonexistent tunables
log_mustnot_expect 'no such tunable: hello' zdb -o hello
log_mustnot_expect 'no such tunable: hello' zdb -o show=hello
log_mustnot_expect 'no such tunable: hello' zdb -o info=hello

# setting a tunable shows the old and the new value
log_must eval 'zdb -o zfs_recover=1 | grep -qE "^zfs_recover: 0 -> 1$"'

# replacing a value still sets it
log_must eval 'zdb -o zfs_recover=0 | grep -qE "^zfs_recover: 0 -> 0$"'

# can't set the "magic" commands
log_mustnot_expect 'no such tunable: 0' zdb -o show=0
log_mustnot_expect 'no such tunable: 1' zdb -o info=1

# can set multiple in same command
log_must eval 'zdb -o zfs_recover=1 -o zfs_flags=512 | xargs | grep -qE "^zfs_recover: 0 -> 1 zfs_flags: 4294932990 -> 512$"'

# can set and show in same command
log_must eval 'zdb -o zfs_recover=1 -o zfs_recover -o zfs_recover=0 | xargs | grep -qE "^zfs_recover: 0 -> 1 zfs_recover: 1 zfs_recover: 1 -> 0$"'

log_pass "zdb can work with libzpool tunables"
