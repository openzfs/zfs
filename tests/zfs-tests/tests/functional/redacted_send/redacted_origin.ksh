#!/bin/ksh

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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Test that receiving sends from redaction bookmarks and redacted datasets
# works correctly in certain edge cases.
# 1. Send A(B,C,D) to pool2.
# 2. Verify send from A(B, C, D) can be received onto it.
# 3. Verify send from A(B, C) can be received onto it.
# 4. Verify send from A() can be received onto it.
# 5. Verify send from A(E) cannot be received onto it.
# 6. Verify send from redaction bookmark for A(B, C) can be received onto it.
# 7. Verify send from redaction bookmark for A() can be received onto it.
# 8. Verify send from redaction bookmark for A(E) cannot be received onto it.
#

typeset ds_name="origin"
typeset sendfs="$POOL/$ds_name"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '' setup_incrementals
typeset dsA=$sendfs@snap0
typeset dsB=$POOL/hole@snap
typeset dsC=$POOL/rm@snap
typeset dsD=$POOL/write@snap
typeset dsE=$POOL/stride3@snap
typeset dsF=$POOL/stride5@snap
typeset targ=$POOL2/targfs@snap

log_onexit redacted_cleanup $sendfs $POOL2/rBCD $POOL2/targfs \
    $POOL2/rBC $POOL2/rE

# Set up all the filesystems and clones.
log_must zfs redact $dsA BCD $dsB $dsC $dsD
log_must eval "zfs send --redact BCD $dsA >$stream"
log_must eval "zfs receive $POOL2/rBCD <$stream"
log_must eval "zfs receive $targ <$stream"

log_must zfs redact $dsA BC $dsB $dsC
log_must eval "zfs send --redact BC $dsA >$stream"
log_must eval "zfs receive $POOL2/rBC <$stream"

log_must zfs redact $dsA E $dsE
log_must eval "zfs send --redact E $dsA >$stream"
log_must eval "zfs receive $POOL2/rE <$stream"

log_must eval "zfs send $dsF >$stream"
log_must eval "zfs receive -o origin=$POOL2/rBCD@snap0 $POOL2/BCDrF <$stream"
log_must eval "zfs receive -o origin=$POOL2/rBC@snap0 $POOL2/BCrF <$stream"
log_must eval "zfs receive -o origin=$POOL2/rE@snap0 $POOL2/ErF <$stream"

# Run tests from redacted datasets.
log_must eval "zfs send -i $POOL2/rBCD@snap0 $POOL2/BCDrF@snap >$stream"
log_must eval "zfs receive -o origin=$targ $POOL2/tdBCD <$stream"

log_must eval "zfs send -i $POOL2/rBC@snap0 $POOL2/BCrF@snap >$stream"
log_must eval "zfs receive -o origin=$targ $POOL2/tdBC <$stream"

log_must eval "zfs send -i $POOL2/rE@snap0 $POOL2/ErF@snap >$stream"
log_mustnot eval "zfs receive -o origin=$targ $POOL2/tdE <$stream"

# Run tests from redaction bookmarks.
log_must eval "zfs send -i $sendfs#BC $dsF >$stream"
log_must eval "zfs receive -o origin=$targ $POOL2/tbBC <$stream"

log_must eval "zfs send -i $sendfs#E $dsF >$stream"
log_mustnot eval "zfs receive -o origin=$targ $POOL2/tbE <$stream"

log_pass "Verify sends from redacted datasets and bookmarks work correctly."
