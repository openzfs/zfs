#!/bin/sh

#
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the Common
# Development and Distribution License ("CDDL"), version 1.0. You may only use
# this file in accordance with the terms of version 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this source. A
# copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

if [ $# -ne 1 ]; then
	echo "Usage: $0 <device>" >&2
	exit 1
fi

set -e

DEVICE="$1"
SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"

zpool create -o ashift=12 test "$DEVICE"

zfs set compression=off recordsize=16MiB test

# We are testing 8MB blocks, so write one short file, 8.5MB
# file, and one 24.5MB file.

"$SCRIPTDIR/gen-lorem-files.py" -d /test -r --min-size 20000 \
    --max-size 24000 1
"$SCRIPTDIR/gen-lorem-files.py" -d /test -r --min-size 8500000 \
    --max-size 8510000 1
"$SCRIPTDIR/gen-lorem-files.py" -d /test -r --min-size 24500000 \
    --max-size 24510000 1

zfs snapshot test@long-payloads
zfs send -L test@long-payloads > /tmp/long-payloads.zsend
