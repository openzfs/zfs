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

case "$(uname -s)" in
Linux)
	while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
		date +%s
		cat /proc/spl/kstat/zfs/zstd
		sleep 1
	done
	;;
FreeBSD)
	while [ -z "${PERF_STOP_FILE:-}" ] || [ ! -f "$PERF_STOP_FILE" ]; do
		date +%s
		sysctl -a kstat.zfs.misc.zstd
		sleep 1
	done
	;;
*)
	exec kstat zfs:0 1
	;;
esac
