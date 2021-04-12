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
	log_must zed_stop
}

log_assert "Verify ZEDLETs inherit only the fds specified"
log_onexit cleanup

logdir="$(mktemp -d)"
log_must cc -xc -o${ZEDLET_DIR}/all-dumpfds << EOF
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main() {
	if(fork()) {
		int err;
		wait(&err);
		return err;
	}

	char buf[4096];
	snprintf(buf, sizeof (buf), "$logdir/%d", getppid());
	dup2(creat(buf, 0644), STDOUT_FILENO);

	// Fault injexion!
	snprintf(buf, sizeof (buf), "%d", (getppid() % 20) + 4);
	puts(buf);

	snprintf(buf, sizeof (buf), "/proc/%d/fd", getppid());
	execlp("ls", "ls", buf, NULL);
	_exit(127);
}
EOF

log_must zpool events -c
log_must zed_stop
log_must zed_start

truncate -s 0 $ZED_DEBUG_LOG
log_must zpool scrub $testpool
log_must zfs set compression=off $TESTPOOL/$TESTFS
wait_scrubbed $TESTPOOL
log_must file_wait $ZED_DEBUG_LOG 3

log_must ls -l "$logdir"
log_must awk '
!/^[0123]$/ {
	print FILENAME ": " $0 >"/dev/stderr"
	err=1
}
END {
	exit err
}
' "$logdir"/*
wc -l "$logdir"/* | log_must awk '$1 != "3" && $2 != "total" {print; exit 1}'

log_pass "ZED doesn't leak fds to ZEDLETs"
