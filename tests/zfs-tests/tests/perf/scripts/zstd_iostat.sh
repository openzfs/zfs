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

: "${PERFPOOL:?PERFPOOL must be set}"

if [ -n "${PERF_START_FILE:-}" ]; then
	while [ ! -f "$PERF_START_FILE" ]; do
		sleep 1
	done
fi

while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
	zpool iostat -lpvyL "$PERFPOOL" 1 1 || exit $?
done
