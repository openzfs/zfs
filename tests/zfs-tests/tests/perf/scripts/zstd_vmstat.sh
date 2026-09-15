#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0

if [ -n "${PERF_START_FILE:-}" ]; then
	while [ ! -f "$PERF_START_FILE" ]; do
		sleep 1
	done
fi

case "$(uname -s)" in
Linux)
	exec vmstat -t 1
	;;
FreeBSD)
	exec vmstat -w 1
	;;
*)
	exec vmstat 1
	;;
esac
