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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, OpenZFS Contributors. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify that zfs receive can accept a redacted send stream generated
# on a system with the opposite byte order.  This is a regression test
# for GitHub issue #18344, which reports that receiving a redacted stream
# from an opposite-endian sender fails.
#
# Strategy:
# 1. Determine the endianness of the running system.
# 2. Select the pre-built redacted send stream from the opposite
#    endianness.
# 3. Decompress the stream and attempt to receive it into POOL2.
# 4. Verify the receive succeeds.
#

verify_runnable "both"

typeset recvfs="$POOL2/opposite_endian_recv"
typeset streamdir=$STF_SUITE/tests/functional/redacted_send
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset sendfile=$(mktemp $tmpdir/stream.XXXX)

function cleanup
{
	if datasetexists $recvfs; then
		destroy_dataset $recvfs "-r"
	fi
	rm -f $sendfile
}
log_onexit cleanup

log_assert "Receiving a redacted stream from an opposite-endian sender" \
    "should succeed (issue #18344)"

# Determine system endianness and select the opposite-endian stream.
typeset native_endian=$(python3 -c "import sys; print(sys.byteorder)")
if [[ "$native_endian" == "little" ]]; then
	# System is little-endian; use the big-endian stream.
	typeset streamfile="$streamdir/big-endian-redacted.zsend.bz2"
	log_note "System is little-endian, receiving big-endian stream"
else
	# System is big-endian; use the little-endian stream.
	typeset streamfile="$streamdir/little-endian-redacted.zsend.bz2"
	log_note "System is big-endian, receiving little-endian stream"
fi

if [[ ! -f "$streamfile" ]]; then
	log_unsupported "Opposite-endian stream file not found: $streamfile"
fi

log_must eval "bzcat <$streamfile >$sendfile"
log_must eval "zfs recv $recvfs <$sendfile"

log_pass "Successfully received opposite-endian redacted stream (issue #18344)"
