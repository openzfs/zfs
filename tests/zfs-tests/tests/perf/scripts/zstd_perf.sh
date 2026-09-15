#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0

: "${PERF_RUNTIME:?PERF_RUNTIME must be set}"

if [ -n "${PERF_START_FILE:-}" ]; then
	while [ ! -f "$PERF_START_FILE" ]; do
		sleep 1
	done
fi

if [ -n "${PERF_OUTPUT_FILE:-}" ]; then
	if [ -n "${PERF_STOP_FILE:-}" ]; then
		exec perf record -F 99 -a -g -q -o "$PERF_OUTPUT_FILE" -- \
		    sh -c 'while [ ! -f "$PERF_STOP_FILE" ]; do sleep 1; done'
	else
		exec perf record -F 99 -a -g -q -o "$PERF_OUTPUT_FILE" -- \
		    sleep "$PERF_RUNTIME"
	fi
else
	exec perf record -F 99 -a -g -q -o /dev/stdout 2>/dev/null -- \
	    sleep "$PERF_RUNTIME"
fi
