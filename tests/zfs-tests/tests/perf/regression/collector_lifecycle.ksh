#!/bin/ksh
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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

verify_runnable "global"

export PERF_RUNTIME=1
export PERF_COLLECT_SCRIPTS=
export PERF_COLLECT_OPTIONAL_SCRIPTS=
export SUDO_COMMAND=collector_lifecycle.ksh

typeset rc
typeset startup_interrupted=0
typeset iostat_pid=
typeset iostat_tmpdir=

function cleanup_iostat_failure
{
	typeset pid=$iostat_pid

	if [[ -n $pid ]]; then
		if kill -0 "$pid" 2>/dev/null; then
			kill -TERM "$pid" 2>/dev/null || :
		fi
		if collect_group_alive "$pid"; then
			kill -TERM -- -"$pid" 2>/dev/null || :
		fi
		if kill -0 "$pid" 2>/dev/null || collect_group_alive "$pid"; then
			sleep 1
		fi
		if kill -0 "$pid" 2>/dev/null; then
			kill -KILL "$pid" 2>/dev/null || :
		fi
		if collect_group_alive "$pid"; then
			kill -KILL -- -"$pid" 2>/dev/null || :
		fi
		wait "$pid" 2>/dev/null || :
		iostat_pid=
	fi
	if [[ -n $iostat_tmpdir ]]; then
		rm -rf "$iostat_tmpdir"
		iostat_tmpdir=
	fi
}

function test_iostat_failure
{
	iostat_tmpdir=$(mktemp -d)
	typeset calls="$iostat_tmpdir/calls"
	typeset status_file="$iostat_tmpdir/status"
	typeset iostat_rc attempts=0

	log_onexit_push cleanup_iostat_failure
	cat > "$iostat_tmpdir/zpool" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$ZPOOL_CALLS"
exit 42
EOF
	chmod 755 "$iostat_tmpdir/zpool"

	PATH="$iostat_tmpdir:$PATH" PERFPOOL=test ZPOOL_CALLS="$calls" \
		zfs_test_setpgid sh -c '
		"$1" > "$2" 2>&1
		rc=$?
		printf "%s\n" "$rc" > "$3"
	' sh "$PERF_SCRIPTS/zstd_iostat.sh" "$iostat_tmpdir/output" \
		"$status_file" &
	iostat_pid=$!
	while [[ ! -f $status_file ]] && (( attempts < 5 )); do
		sleep 1
		((attempts += 1))
	done
	if [[ ! -f $status_file ]]; then
		cleanup_iostat_failure
		log_onexit_pop
		log_fail "zstd_iostat did not stop after a failed sample"
	fi
	wait "$iostat_pid" 2>/dev/null || :
	iostat_pid=
	iostat_rc=$(cat "$status_file")
	(( iostat_rc == 42 )) || log_fail "zstd_iostat hid a sample failure"
	(( $(wc -l < "$calls") == 1 )) || \
		log_fail "zstd_iostat retried a failed sample"
	cleanup_iostat_failure
	log_onexit_pop
}

function cleanup
{
	do_collect_scripts_cleanup
	rm -f "$(get_perf_output_dir)"/collector_lifecycle.*
}

log_onexit cleanup

function run_collector
{
	typeset command=$1
	typeset tag=$2

	collect_scripts=("$command" "$tag")
	log_onexit_push do_collect_scripts_cleanup
	log_must do_collect_scripts "$tag"
	for start_file in "${collect_start_files[@]}"; do
		: > "$start_file"
	done
}

function stop_collector
{
	typeset expected=$1

	rc=0
	do_collect_scripts_stop || rc=$?
	log_onexit_pop
	(( rc == expected ))
}

test_iostat_failure

function test_interrupted_startup
{
	typeset killer_pid
	typeset base="$(get_perf_output_dir)/collector_lifecycle.ksh"
	base="$base.interrupted.interrupted"

	export ZFS_TEST_SETPGID_DELAY=5
	collect_scripts=('printf interrupted; exec sleep 30' interrupted)
	log_onexit_push do_collect_scripts_cleanup
	trap 'startup_interrupted=1; do_collect_scripts_cleanup' INT
	( sleep 1; kill -INT $$ ) &
	killer_pid=$!
	do_collect_scripts interrupted || :
	wait "$killer_pid" 2>/dev/null || :
	trap - INT
	log_onexit_pop
	unset ZFS_TEST_SETPGID_DELAY

	(( startup_interrupted == 1 )) || \
		log_fail "startup interruption was ignored"
	sleep 6
	for marker in "$base.start" "$base.stop" "$base.ready"; do
		[[ ! -e $marker ]] || log_fail "startup marker survived interruption"
	done
}

# Interrupt the harness while its collector is still before setpgid().
test_interrupted_startup

# A required collector failure remains visible when it leaves a descendant.
run_collector 'printf failure; sleep 30 & exit 42' failed
stop_collector 1 || log_fail "required collector failure was hidden"

# A collector terminated by the harness with SIGTERM has Ksh's signal status.
run_collector 'printf term; exec sleep 30' term
stop_collector 0 || log_fail "expected SIGTERM termination was rejected"

# A collector that ignores SIGTERM is escalated to SIGKILL and still succeeds.
run_collector 'printf kill; trap "" TERM; while :; do sleep 1; done' kill
stop_collector 0 || log_fail "expected SIGKILL termination was rejected"

# Delay process-group creation so shutdown coverage depends on the readiness
# handshake rather than a race between launch and the first stop check.
export ZFS_TEST_SETPGID_DELAY=2
run_collector 'printf delayed; exec sleep 30' delayed
unset ZFS_TEST_SETPGID_DELAY
stop_collector 0 || log_fail "delayed collector did not shut down cleanly"

log_pass "collector lifecycle harness coverage passed"
