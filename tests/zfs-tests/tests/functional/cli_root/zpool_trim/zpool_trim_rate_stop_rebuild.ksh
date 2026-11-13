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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
#	Stopping a sequential rebuild does not wait for a rate-limited manual
#	TRIM, when the three metaslabs a top-level vdev allows to be disabled
#	at once are held by manual TRIMs.
#
# STRATEGY:
#	1. Create a pool on a four-way mirror of 1 GiB files, whose metaslabs
#	   are 32 MiB, and limit TRIM I/Os to 16 MiB.  At 90 KiB/s a manual
#	   TRIM holds a metaslab disabled for about three minutes.
#	2. Trim the first three files quickly to 10%, 40% and 70%, suspend
#	   them and resume them at 90 KiB/s, so that each holds a different
#	   metaslab, and verify that they are still trimming.
#	3. Attach a fifth file with a sequential rebuild, whose thread waits
#	   for a metaslab, and verify that detaching it, which stops the
#	   rebuild, does not wait for the manual TRIMs.
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi

	log_must set_tunable64 TRIM_EXTENT_BYTES_MAX $trim_extent_bytes_max
}
log_onexit cleanup

# Trim a file quickly to the given progress, then resume it slowly.
function trim_to_then_slow # file progress
{
	typeset file=$1
	typeset -i target=$2
	typeset -i progress=0
	typeset -i i

	log_must zpool trim -r 64M $TESTPOOL $file
	for (( i = 0; i < 600; i++ )); do
		progress=$(trim_progress $TESTPOOL $file)
		(( progress >= target )) && break
		log_must sleep 0.1
	done
	log_must zpool trim -s $TESTPOOL $file
	progress=$(trim_progress $TESTPOOL $file)
	(( progress >= target && progress < 100 )) || \
	    log_fail "TRIM of $file suspended at $progress%"
	log_must zpool trim -r $SLOW $TESTPOOL $file
}

# Run a command and fail if it takes 30 seconds or more.
function must_be_quick # command...
{
	typeset -i start=$SECONDS

	log_must "$@"
	(( SECONDS - start < 30 )) || \
	    log_fail "'$*' took $(( SECONDS - start ))s"
}

SLOW=90K
typeset trim_extent_bytes_max=$(get_tunable TRIM_EXTENT_BYTES_MAX)
typeset -a FILES
for i in 1 2 3 4 5; do
	FILES[$i]="$TESTDIR/trim-stop-wait-$i"
done

log_must mkdir "$TESTDIR"
log_must truncate -s 1G ${FILES[1]} ${FILES[2]} ${FILES[3]} ${FILES[4]} \
    ${FILES[5]}
log_must zpool create -f -o autotrim=off $TESTPOOL \
    mirror ${FILES[1]} ${FILES[2]} ${FILES[3]} ${FILES[4]}
log_must set_tunable64 TRIM_EXTENT_BYTES_MAX $((16 * 1024 * 1024))

trim_to_then_slow ${FILES[1]} 10
trim_to_then_slow ${FILES[2]} 40
trim_to_then_slow ${FILES[3]} 70
log_must sleep 5
for i in 1 2 3; do
	log_must eval "trim_prog_line $TESTPOOL ${FILES[$i]} | \
	    grep -q 'trimmed, started'"
done

log_must zpool attach -s $TESTPOOL ${FILES[1]} ${FILES[5]}
log_must sleep 5
must_be_quick zpool detach $TESTPOOL ${FILES[5]}

log_pass "Stopping a rebuild does not wait for a manual TRIM"
