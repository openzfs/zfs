#!/bin/sh
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

set -eu

if [ -n "${PERF_START_FILE:-}" ]; then
	while [ ! -f "$PERF_START_FILE" ]; do
		sleep 1
	done
fi

vmstat_pid=
stop_vmstat()
{
	if [ -n "$vmstat_pid" ]; then
		kill "$vmstat_pid" 2>/dev/null || :
		wait "$vmstat_pid" 2>/dev/null || :
		vmstat_pid=
	fi
}

cleanup()
{
	stop_vmstat
}

trap 'exit 0' HUP INT TERM
trap cleanup EXIT

case "$(uname -s)" in
Linux)
	while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
		vmstat -t -y 1 1
	done
	;;
FreeBSD)
	vmstat -w 1 &
	vmstat_pid=$!
	while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
		if ! kill -0 "$vmstat_pid" 2>/dev/null; then
			rc=0
			wait "$vmstat_pid" 2>/dev/null || rc=$?
			vmstat_pid=
			[ "$rc" -ne 0 ] && exit "$rc"
			exit 1
		fi
		sleep 1
	done
	stop_vmstat
	;;
*)
	vmstat 1 &
	vmstat_pid=$!
	while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
		if ! kill -0 "$vmstat_pid" 2>/dev/null; then
			rc=0
			wait "$vmstat_pid" 2>/dev/null || rc=$?
			vmstat_pid=
			[ "$rc" -ne 0 ] && exit "$rc"
			exit 1
		fi
		sleep 1
	done
	stop_vmstat
	;;
esac
