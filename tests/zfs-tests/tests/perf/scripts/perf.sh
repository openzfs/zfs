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

: "${PERF_RUNTIME:?PERF_RUNTIME must be set}"

if [ -n "${PERF_START_FILE:-}" ]; then
	while [ ! -f "$PERF_START_FILE" ]; do
		sleep 1
	done
fi

if [ -n "${PERF_OUTPUT_FILE:-}" ]; then
	if [ -n "${PERF_STOP_FILE:-}" ]; then
		exec perf record -F 99 -a -g -q -o "$PERF_OUTPUT_FILE" -- \
		    sh -c "while [ ! -f \"\$1\" ]; do sleep 1; done" sh \
		    "$PERF_STOP_FILE"
	else
		exec perf record -F 99 -a -g -q -o "$PERF_OUTPUT_FILE" -- \
		    sleep "$PERF_RUNTIME"
	fi
else
	exec perf record -F 99 -a -g -q -o /dev/stdout 2>/dev/null -- \
	    sleep "$PERF_RUNTIME"
fi
