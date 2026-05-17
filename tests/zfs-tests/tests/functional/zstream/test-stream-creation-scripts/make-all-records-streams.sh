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

DEVICE="$1"
SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"

zpool create -o ashift=12 test "$DEVICE"
zfs set compression=on xattr=sa test
zfs create test/source

"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/source --min-size 2048 \
    --max-size 32000 3
"$SCRIPTDIR/add-xattrs.py" /test/source/*
echo "very small" > /test/source/small
echo "password" > /test/source/to-be-redacted
chmod 400 /test/source/to-be-redacted

zfs snapshot -r test/source@baseline
zfs clone test/source@baseline test/redacted
rm /test/redacted/to-be-redacted
"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/redacted --min-size 4096 \
    --max-size 32000 3
"$SCRIPTDIR/add-xattrs.py" /test/redacted/*
cd /test/redacted || exit 1
tar cf /tmp/dups.tar .
mkdir copies
cd copies || exit 1
tar xvf /tmp/dups.tar

echo "password" > /test/redacted/new-key
zfs create -o encryption=on -o keylocation=file:///test/redacted/new-key \
    -o keyformat=passphrase test/redacted/encrypted
"$SCRIPTDIR/gen-lorem-files.py" -r -d /test/redacted/encrypted 3
echo "very small" > /test/redacted/encrypted/small-encrypted
# "$SCRIPTDIR/add-xattrs.py" /test/redacted/encrypted/*

zfs snapshot -r test/redacted@clean

zfs redact test/source@baseline redaction-bookmark test/redacted@clean
zfs send -ce --redact redaction-bookmark test/source@baseline \
    > /tmp/all-record-types-base.zsend
zfs send -Rcew -i test/source@baseline test/redacted@clean \
    > /tmp/all-record-types-incr.zsend
