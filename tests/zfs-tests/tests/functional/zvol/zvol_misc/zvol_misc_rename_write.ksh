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
# Verify that renaming a ZVOL, or a dataset above it, does not make writes
# to the ZVOL fail, and that the /dev/zvol links are still updated.
#
# STRATEGY:
# 1. Create a ZVOL and keep writing to it with O_DIRECT writes
# 2. Rename the ZVOL back and forth, then once more to a second name
# 3. Verify that none of the writes failed
# 4. Verify that the link for the final name exists and the other one is gone
# 5. Rename the parent dataset of another ZVOL and verify its links
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Only Linux has a /dev/zvol link per ZVOL name"
fi

function cleanup
{
	[[ -n $STOP ]] && touch $STOP
	[[ -n $WRITER ]] && wait $WRITER
	destroy_dataset "$PARENT-renamed" "-rf"
	destroy_dataset "$PARENT" "-rf"
	destroy_dataset "$VOL-renamed" "-rf"
	destroy_dataset "$VOL" "-rf"
	[[ -n $WORKDIR ]] && rm -rf $WORKDIR
	block_device_wait
}

#
# Wait for udev to process its queue and for a /dev/zvol link to point to
# the given device node, or to be gone if no node is given.  This must not
# use block_device_wait(), which triggers change events and would hide a
# change event the kernel did not send.
#
function link_wait # link [node]
{
	typeset link=$1
	typeset node=$2
	typeset -i i

	for ((i = 0; i < 30; i++)); do
		udevadm settle
		if [[ -z $node ]]; then
			[[ ! -e $link && ! -L $link ]] && return 0
		else
			[[ -e $link && $(readlink -f $link) == $node ]] && \
			    return 0
		fi
		sleep 1
	done
	return 1
}

#
# Print the device node a ZVOL's /dev/zvol link points to, once udev has
# created the link.
#
function zvol_node # volume
{
	typeset link="$ZVOL_DEVDIR/$1"
	typeset -i i

	for ((i = 0; i < 30; i++)); do
		udevadm settle
		[[ -b $link ]] && break
		sleep 1
	done
	[[ -b $link ]] || return 1
	readlink -f $link
}

log_assert "Renaming a ZVOL does not fail writes and updates the links"
log_onexit cleanup

typeset -i i
VOL="$TESTPOOL/vol.$$"
PARENT="$TESTPOOL/parent.$$"
WRITER=""
WORKDIR=$(mktemp -d $TEST_BASE_DIR/zvol_misc_rename_write.XXXXXX) || \
    log_fail "Failed to create a work directory"
STOP="$WORKDIR/stop"
ERRORS="$WORKDIR/errors"
PASSES="$WORKDIR/passes"

# 1. Create a ZVOL and keep writing to it with O_DIRECT writes
log_must zfs create -V 64m "$VOL"
ZNODE=$(zvol_node "$VOL") || log_fail "No device for $VOL"

(
	while [[ ! -e $STOP ]]; do
		dd if=/dev/zero of=$ZNODE bs=4k count=4096 oflag=direct \
		    status=none && echo >>$PASSES
	done 2>$ERRORS
) &
WRITER=$!

for ((i = 0; i < 120; i++)); do
	[[ -s $PASSES ]] && break
	sleep 1
done
[[ -s $PASSES ]] || log_fail "The writer did not complete a pass"

# 2. Rename the ZVOL back and forth, then once more to a second name
for ((i = 0; i < 15; i++)); do
	log_must zfs rename "$VOL" "$VOL-renamed"
	log_must zfs rename "$VOL-renamed" "$VOL"
done
log_must zfs rename "$VOL" "$VOL-renamed"

# 3. Verify that none of the writes failed
log_must touch $STOP
wait $WRITER
WRITER=""
log_note "The writer completed $(wc -l <$PASSES) passes"
if [[ -s $ERRORS ]]; then
	log_note "Failed writes while renaming:"
	log_note "$(sort $ERRORS | uniq -c)"
	log_fail "Writes to $ZNODE failed while the ZVOL was renamed"
fi

# 4. Verify that the link for the final name exists and the other one is gone
link_wait "$ZVOL_DEVDIR/$VOL-renamed" $ZNODE ||
    log_fail "$ZVOL_DEVDIR/$VOL-renamed does not point to $ZNODE"
link_wait "$ZVOL_DEVDIR/$VOL" ||
    log_fail "$ZVOL_DEVDIR/$VOL still exists"

# 5. Rename the parent dataset of another ZVOL and verify its links
log_must zfs create "$PARENT"
log_must zfs create -V 64m "$PARENT/vol"
ZNODE=$(zvol_node "$PARENT/vol") || log_fail "No device for $PARENT/vol"

log_must zfs rename "$PARENT" "$PARENT-renamed"
link_wait "$ZVOL_DEVDIR/$PARENT-renamed/vol" $ZNODE ||
    log_fail "$ZVOL_DEVDIR/$PARENT-renamed/vol does not point to $ZNODE"
link_wait "$ZVOL_DEVDIR/$PARENT/vol" ||
    log_fail "$ZVOL_DEVDIR/$PARENT/vol still exists"

log_pass "Renaming a ZVOL does not fail writes and updates the links"
