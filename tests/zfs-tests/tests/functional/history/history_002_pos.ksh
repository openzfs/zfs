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

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Create a  scenario to verify the following zfs subcommands are logged.
#	create, destroy, clone, rename, snapshot, rollback, set, inherit,
#	receive, promote, hold and release.
#
# STRATEGY:
#	1. Verify that all the zfs commands listed (barring send) produce an
#	   entry in the pool history.
#

verify_runnable "global"

function cleanup
{

	[[ -f $tmpfile ]] && rm -f $tmpfile
	[[ -f $tmpfile2 ]] && rm -f $tmpfile2
	for dataset in $fs $newfs $fsclone $vol $newvol $volclone; do
		datasetexists $dataset && destroy_dataset $dataset -Rf
	done
	rm -rf /history.$$
}

log_assert "Verify zfs sub-commands which modify state are logged."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS1; newfs=$TESTPOOL/newfs; fsclone=$TESTPOOL/clone
vol=$TESTPOOL/$TESTVOL ; newvol=$TESTPOOL/newvol; volclone=$TESTPOOL/volclone
fssnap=$fs@fssnap; fssnap2=$fs@fssnap2
volsnap=$vol@volsnap; volsnap2=$vol@volsnap2
tmpfile=$TEST_BASE_DIR/tmpfile.$$ ; tmpfile2=$TEST_BASE_DIR/tmpfile2.$$

if is_linux; then
#	property	value		property	value
#
props=(
	quota		64M		recordsize	512
	reservation	32M		reservation	none
	mountpoint	/history.$$	mountpoint	legacy
	mountpoint	none		compression	lz4
	compression	on		compression	off
	compression	lzjb		acltype		off
	acltype		posix		acltype		nfsv4
	atime		on		atime		off
	devices		on		devices		off
	exec		on		exec		off
	setuid		on		setuid		off
	readonly	on		readonly	off
	zoned		on		zoned		off
	snapdir		hidden		snapdir		visible
	aclinherit	discard		aclinherit	noallow
	aclinherit	secure		aclinherit	passthrough
	canmount	off		canmount	on
	compression	gzip		compression	gzip-$((RANDOM%9 + 1))
	compression     zstd		compression	zstd-$((RANDOM%9 + 1))
	compression	zstd-fast	copies          $((RANDOM%3 + 1))
	compression	zstd-fast-$((RANDOM%9 + 1))	xattr	sa
	xattr		on		xattr		off
)
elif is_freebsd; then
#	property	value		property	value
#
props=(
	quota		64M		recordsize	512
	reservation	32M		reservation	none
	mountpoint	/history.$$	mountpoint	legacy
	mountpoint	none		sharenfs	on
	sharenfs	off
	compression	on		compression	off
	compression	lzjb		aclmode		discard
	aclmode		groupmask	aclmode		passthrough
	atime		on		atime		off
	devices		on		devices		off
	exec		on		exec		off
	setuid		on		setuid		off
	readonly	on		readonly	off
	jailed		on		jailed		off
	snapdir		hidden		snapdir		visible
	aclinherit	discard		aclinherit	noallow
	aclinherit	secure		aclinherit	passthrough
	canmount	off		canmount	on
	compression	gzip		compression	gzip-$((RANDOM%9 + 1))
	compression     zstd		compression	zstd-$((RANDOM%9 + 1))
	compression	zstd-fast	copies          $((RANDOM%3 + 1))
	compression	zstd-fast-$((RANDOM%9 + 1))	acltype	off
	acltype		posix		acltype		nfsv4
)
else
#	property	value		property	value
#
props=(
	quota		64M		recordsize	512
	reservation	32M		reservation	none
	mountpoint	/history.$$	mountpoint	legacy
	mountpoint	none		sharenfs	on
	sharenfs	off
	compression	on		compression	off
	compression	lzjb		aclmode		discard
	aclmode		groupmask	aclmode		passthrough
	atime		on		atime		off
	devices		on		devices		off
	exec		on		exec		off
	setuid		on		setuid		off
	readonly	on		readonly	off
	zoned		on		zoned		off
	snapdir		hidden		snapdir		visible
	aclinherit	discard		aclinherit	noallow
	aclinherit	secure		aclinherit	passthrough
	canmount	off		canmount	on
	xattr		on		xattr		off
	compression	gzip		compression	gzip-$((RANDOM%9 + 1))
	copies		$((RANDOM%3 + 1))
)
fi

run_and_verify "zfs create $fs"
# Set all the property for filesystem
typeset -i i=0
while ((i < ${#props[@]})) ; do
	run_and_verify "zfs set ${props[$i]}=${props[((i+1))]} $fs"

	# quota, reservation, canmount can not be inherited.
	#
	if [[ ${props[$i]} != "quota" && ${props[$i]} != "reservation" && \
	    ${props[$i]} != "canmount" ]];
	then
		run_and_verify "zfs inherit ${props[$i]} $fs"
	fi

	((i += 2))
done

run_and_verify "zfs create -V 64M $vol"
run_and_verify "zfs set volsize=32M $vol"
run_and_verify "zfs snapshot $fssnap"
run_and_verify "zfs hold tag $fssnap"
run_and_verify "zfs release tag $fssnap"
run_and_verify "zfs snapshot $volsnap"
run_and_verify "zfs snapshot $fssnap2"
run_and_verify "zfs snapshot $volsnap2"

# Send isn't logged...
log_must eval "zfs send -i $fssnap $fssnap2 > $tmpfile"
log_must eval "zfs send -i $volsnap $volsnap2 > $tmpfile2"
# Verify that's true
zpool history $TESTPOOL | grep 'zfs send' >/dev/null 2>&1 && \
    log_fail "'zfs send' found in history of \"$TESTPOOL\""

run_and_verify "zfs destroy $fssnap2"
run_and_verify "zfs destroy $volsnap2"
run_and_verify "zfs receive $fs < $tmpfile"
run_and_verify "zfs receive $vol < $tmpfile2"
run_and_verify "zfs rollback -r $fssnap"
run_and_verify "zfs rollback -r $volsnap"
run_and_verify "zfs clone $fssnap $fsclone"
run_and_verify "zfs clone $volsnap $volclone"
run_and_verify "zfs rename $fs $newfs"
run_and_verify "zfs rename $vol $newvol"
run_and_verify "zfs promote $fsclone"
run_and_verify "zfs promote $volclone"
run_and_verify "zfs destroy $newfs"
run_and_verify "zfs destroy $newvol"
run_and_verify "zfs destroy -rf $fsclone"
run_and_verify "zfs destroy -rf $volclone"

log_pass "zfs sub-commands which modify state are logged passed."
