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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

verify_runnable "both"

src=$POOL/promoted-origin
dst=$POOL2/promoted-origin
external=$POOL/promoted-external
external_dst=$POOL2/promoted-external
key=
stream=$BACKDIR/promoted-origin.stream

function cleanup_datasets
{
	typeset ds
	for ds in $src $dst; do
		if datasetexists $ds/clone &&
		    [[ $(get_prop origin $ds) != "-" ]]; then
			log_must zfs rename $ds/clone $ds-origin
		fi
		datasetexists $ds && log_must zfs destroy -R $ds
		datasetexists $ds-origin && log_must zfs destroy -R $ds-origin
	done
}

function cleanup
{
	cleanup_datasets
	datasetexists $external && log_must zfs destroy -r $external
	datasetexists $external_dst && log_must zfs destroy -r $external_dst
	[[ -n $key ]] && log_must rm -f $key
	log_must rm -f $stream
}

for ds in $src $dst $src-origin $dst-origin $external $external_dst; do
	log_mustnot datasetexists $ds
done
log_onexit cleanup
log_assert "Replicating promoted origins preserves reused snapshot names"

key=$(mktemp $TEST_BASE_DIR/promoted-origin.XXXXXX)
log_must test -f $key
log_must eval "openssl rand -hex 32 > '$key'"

for encrypted in off on; do
	set -A cryptopts
	if [[ $encrypted == on ]]; then
		set -A cryptopts -- -o encryption=aes-256-gcm -o keyformat=hex \
		    -o keylocation=file://$key
	fi
	log_must zfs create -o mountpoint=$TESTDIR/promoted-origin \
	    "${cryptopts[@]}" $src
	log_must dd if=/dev/urandom of=$TESTDIR/promoted-origin/data \
	    bs=1M count=1
	log_must zfs snapshot $src@older
	log_must zfs snapshot $src@base
	log_must zfs clone $src@base $src/clone
	log_must zfs promote $src/clone
	log_must dd if=/dev/urandom of=$TESTDIR/promoted-origin/data \
	    bs=1M count=1
	log_must dd if=/dev/urandom of=$TESTDIR/promoted-origin/clone/data \
	    bs=1M count=1
	log_must zfs snapshot $src@older
	log_must zfs snapshot $src@base
	log_must zfs snapshot -r $src@final
	log_must zfs send -nwR $src@final
	log_must eval "zfs send -wR $src@final > '$stream'"
	log_must eval "zfs receive -un $dst < '$stream'"
	log_mustnot datasetexists $dst
	log_must eval "zfs receive -u $dst < '$stream'"
	if [[ $encrypted == on ]]; then
		log_must eval "[[ \$(get_prop encryptionroot $dst) == $dst/clone ]]"
		log_must eval "[[ \$(get_prop keystatus $dst) == unavailable ]]"
		log_must zfs load-key -L file://$key $dst/clone
	fi
	log_must zfs set mountpoint=$TESTDIR/promoted-received $dst
	ismounted $dst || log_must zfs mount $dst
	ismounted $dst/clone || log_must zfs mount $dst/clone
	log_must cmp $TESTDIR/promoted-origin/data \
	    $TESTDIR/promoted-received/data
	log_must cmp $TESTDIR/promoted-origin/clone/data \
	    $TESTDIR/promoted-received/clone/data
	log_must eval "[[ \$(get_prop origin $dst) == $dst/clone@base ]]"
	for suffix in "" /clone; do
		for snap in older base final; do
			log_must eval "[[ \$(get_prop guid $src$suffix@$snap) == \
			    \$(get_prop guid $dst$suffix@$snap) ]]"
		done
	done
	cleanup_datasets
done

log_must zfs create -o mountpoint=$TESTDIR/promoted-external $external
log_must dd if=/dev/urandom of=$TESTDIR/promoted-external/data bs=1M count=1
log_must zfs snapshot $external@base
log_must eval "zfs send $external@base > '$stream'"
log_must eval "zfs receive -u $external_dst < '$stream'"
log_must zfs clone $external@base $src
log_must zfs snapshot $src@base
log_must zfs clone $src@base $src/clone
log_must zfs promote $src/clone
log_must zfs snapshot -r $src@final
log_must zfs send -nR $src@final
log_must eval "zfs send -R $src@final > '$stream'"
log_must eval "zfs receive -un $dst < '$stream'"
log_mustnot datasetexists $dst
log_must eval "zfs receive -u $dst < '$stream'"
log_must eval "[[ \$(get_prop origin $dst/clone) == $external_dst@base ]]"
log_must zfs set mountpoint=$TESTDIR/promoted-received $dst/clone
ismounted $dst/clone || log_must zfs mount $dst/clone
log_must cmp $TESTDIR/promoted-external/data $TESTDIR/promoted-received/data

log_pass "Promoted origins retain their data, snapshot GUIDs and lineage"
