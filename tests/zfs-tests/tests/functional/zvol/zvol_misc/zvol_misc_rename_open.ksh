#!/bin/ksh -p
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
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that a ZVOL can be opened while it, or a dataset above it, is
# being renamed.
#
# STRATEGY:
# 1. Create a ZVOL and keep opening its device to read a block, with no
#    other opener, so that each open is a first open
# 2. Rename the ZVOL back and forth, then the dataset above it
# 3. Verify that every open succeeded
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Only Linux keeps the device of a ZVOL across a rename"
fi

function cleanup
{
	[[ -n $STOP ]] && touch $STOP
	[[ -n $READER ]] && wait $READER
	destroy_dataset "$PARENT-renamed" "-rf"
	destroy_dataset "$PARENT" "-rf"
	[[ -n $WORKDIR ]] && rm -rf $WORKDIR
	block_device_wait
}

log_assert "A ZVOL can be opened while it is being renamed"
log_onexit cleanup

typeset -i i
PARENT="$TESTPOOL/parent.$$"
VOL="$PARENT/vol"
READER=""
WORKDIR=$(mktemp -d $TEST_BASE_DIR/zvol_misc_rename_open.XXXXXX) || \
    log_fail "Failed to create a work directory"
STOP="$WORKDIR/stop"
ERRORS="$WORKDIR/errors"
PASSES="$WORKDIR/passes"

# 1. Create a ZVOL and keep opening its device to read a block
log_must zfs create "$PARENT"
log_must zfs create -V 64m "$VOL"
block_device_wait "$ZVOL_DEVDIR/$VOL"
ZNODE=$(readlink -f "$ZVOL_DEVDIR/$VOL")
log_must test -b "$ZNODE"

(
	while [[ ! -e $STOP ]]; do
		dd if=$ZNODE of=/dev/null bs=4k count=1 iflag=direct \
		    status=none && echo >>$PASSES
	done 2>$ERRORS
) &
READER=$!

for ((i = 0; i < 120; i++)); do
	[[ -s $PASSES ]] && break
	sleep 1
done
[[ -s $PASSES ]] || log_fail "The reader did not complete a pass"

# 2. Rename the ZVOL back and forth, then the dataset above it
for ((i = 0; i < 25; i++)); do
	log_must zfs rename "$VOL" "$VOL-renamed"
	log_must zfs rename "$VOL-renamed" "$VOL"
done
for ((i = 0; i < 25; i++)); do
	log_must zfs rename "$PARENT" "$PARENT-renamed"
	log_must zfs rename "$PARENT-renamed" "$PARENT"
done

# 3. Verify that every open succeeded
log_must touch $STOP
wait $READER
READER=""
log_note "The reader completed $(wc -l <$PASSES) passes"
if [[ -s $ERRORS ]]; then
	log_note "Failed reads while renaming:"
	log_note "$(sort $ERRORS | uniq -c)"
	log_fail "Opening $ZNODE failed while the ZVOL was renamed"
fi

log_pass "A ZVOL can be opened while it is being renamed"
