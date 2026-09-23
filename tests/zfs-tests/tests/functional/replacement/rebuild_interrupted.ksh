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

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/rebuild_test.kshlib

#
# DESCRIPTION:
#	An interrupted rebuild keeps the failed writes it has counted.
#
# STRATEGY:
#	1. Fail destination writes, then attach another device so the rebuild
#	   restarts. The reset must report the earlier errors, and the new
#	   pass must complete without any.
#	Heal and compare data after export/import.
#

verify_runnable "global"
log_assert "Interrupted rebuilds keep their failed-write accounting"
rebuild_test_init

# One chunk in flight and slow source reads keep the rebuild running while a
# device is attached.
log_must set_tunable64 REBUILD_VDEV_LIMIT 1

log_note "Resetting a rebuild after failed writes"
rebuild_test_create 2 mirror
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s "$TESTPOOL1" "$rebuild_dir/disk-0" "$rebuild_target"
rebuild_test_inject -d "$rebuild_target" -e noop -T write -f 100
dropped=$rebuild_inject_last
rebuild_test_inject -d "$rebuild_target" -e io -T write -f 100
failed=$rebuild_inject_last
rebuild_test_inject -d "$rebuild_dir/disk-0" -D 100:1 -T read
rebuild_test_inject -d "$rebuild_dir/disk-1" -D 100:1 -T read
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must rebuild_test_wait_fired $failed
log_must rebuild_test_clear_faults $dropped $failed
log_must truncate -s 512M "$rebuild_dir/disk-extra"
log_must zpool attach -s "$TESTPOOL1" "$rebuild_dir/disk-1" \
    "$rebuild_dir/disk-extra"
log_must rebuild_test_clear_faults
rebuild_test_completed 0
log_must grep -E " rebuild .* errors=[1-9][0-9]* reset$" \
    "$rebuild_dir/history"
log_must rebuild_test_wait replace
rebuild_test_finish

log_pass "Interrupted rebuilds kept their failed-write accounting"
