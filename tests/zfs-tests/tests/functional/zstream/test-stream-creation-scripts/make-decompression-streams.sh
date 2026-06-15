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
echo "password" > /test/password

zfs create -o compression=zstd-5 test/unencrypted
"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/unencrypted --min-size 12000 \
    --max-size 40000 2
"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/unencrypted --min-size 140000 \
    --max-size 160000 1

zfs create -o compression=lz4 -o encryption=on \
    -o keylocation=file:///test/password -o keyformat=passphrase test/encrypted
"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/encrypted --min-size 12000 \
    --max-size 40000 3

zfs snapshot -r test@decompression
zfs send -cw test/unencrypted@decompression > /tmp/decompression.zsend
zfs send -cw test/encrypted@decompression > /tmp/decompression-crypt.zsend
