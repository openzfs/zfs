#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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

# DESCRIPTION:
# Verify ZEDLETs only inherit the fds specified in the manpage
#
# STRATEGY:
# 1. Inject a ZEDLET that dumps the fds it gets to a file.
# 2. Generate some events.
# 3. Read back the generated files and assert that there is no fd past 3,
#    and there are exactly 4 fds.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

function cleanup
{
	log_must rm -rf "$logdir"
	log_must rm "/tmp/zts-zed_fd_spill-logdir"
	log_must zed_stop
}

log_assert "Verify ZEDLETs inherit only the fds specified"
log_onexit cleanup

logdir="$(mktemp -d)"
log_must ln -s "$logdir" /tmp/zts-zed_fd_spill-logdir


zedlet="$(command -v zed_fd_spill-zedlet)"
log_must ln -s "$zedlet" "${ZEDLET_DIR}/all-dumpfds"

# zed will cry foul and refuse to run it if this isn't true
sudo chown root "$zedlet"
sudo chmod 700 "$zedlet"

log_must zpool events -c
log_must zed_stop
log_must zed_start

log_must truncate -s 0 $ZED_DEBUG_LOG
log_must zpool scrub $TESTPOOL
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must wait_scrubbed $TESTPOOL
log_must file_wait $ZED_DEBUG_LOG 3

if [ -n "$(find "$logdir" -maxdepth 0 -empty)" ]; then
	log_fail "Our ZEDLET didn't run!"
fi
log_must awk '
	!/^[0123]$/ {
		print FILENAME ": " $0
		err=1
	}
	END {
		exit err
	}
' "$logdir"/*
wc -l "$logdir"/* | log_must awk '$1 != "4" && $2 != "total" {print; exit 1}'

log_pass "ZED doesn't leak fds to ZEDLETs"
