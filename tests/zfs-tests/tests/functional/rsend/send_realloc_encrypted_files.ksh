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
# Copyright (c) 2019, Lawrence Livermore National Security LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify encrypted raw incremental receives handle dnode reallocation.

# Strategy:
# 1. Create a pool containing an encrypted filesystem.
# 2. Use 'zfs send -wp' to perform a raw send of the initial filesystem.
# 3. Repeat the following steps N times to verify raw incremental receives.
#   a) Randomly change several key dataset properties.
#   b) Modify the contents of the filesystem such that dnode reallocation
#      is likely during the 'zfs receive', and receive_object() exercises
#      as much of its functionality as possible.
#   c) Create a new snapshot and generate an raw incremental stream.
#   d) Receive the raw incremental stream and verify the received contents.
#   e) Destroy the incremental stream and old snapshot.
#

verify_runnable "both"

log_assert "Verify encrypted raw incremental receive handles reallocation"

function cleanup
{
	rm -f $BACKDIR/fs@*
	rm -f $keyfile
	destroy_dataset $POOL/fs "-rR"
	destroy_dataset $POOL/newfs "-rR"
}

log_onexit cleanup

typeset keyfile=/$TESTPOOL/pkey

# Create an encrypted dataset
log_must eval "echo 'password' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $POOL/fs

last_snap=1
log_must zfs snapshot $POOL/fs@snap${last_snap}
log_must eval "zfs send -wp $POOL/fs@snap${last_snap} \
    >$BACKDIR/fs@snap${last_snap}"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs@snap${last_snap}"

# Set atime=off to prevent the recursive_cksum from modifying newfs.
log_must zfs set atime=off $POOL/newfs

if is_kmemleak; then
	# Use fewer files and passes on debug kernels
	# to avoid timeout due to reduced performance.
	nr_files=100
	passes=2
else
	nr_files=300
	passes=3
fi

for i in {1..$passes}; do
	# Randomly modify several dataset properties in order to generate
	# more interesting incremental send streams.
	rand_set_prop $POOL/fs checksum "off" "fletcher4" "sha256"
	rand_set_prop $POOL/fs compression "${compress_prop_vals[@]}"
	rand_set_prop $POOL/fs recordsize "32K" "128K"
	rand_set_prop $POOL/fs dnodesize "legacy" "auto" "4k"
	rand_set_prop $POOL/fs xattr "on" "sa"

	# Churn the filesystem in such a way that we're likely to be both
	# allocating and reallocating objects in the incremental stream.
	log_must churn_files $nr_files 524288 $POOL/fs
	expected_cksum=$(recursive_cksum /$POOL/fs)

	# Create a snapshot and use it to send an incremental stream.
	this_snap=$((last_snap + 1))
	log_must zfs snapshot $POOL/fs@snap${this_snap}
	log_must eval "zfs send -wp -i $POOL/fs@snap${last_snap} \
	    $POOL/fs@snap${this_snap} > $BACKDIR/fs@snap${this_snap}"

	# Receive the incremental stream and verify the received contents.
	log_must eval "zfs recv -Fu $POOL/newfs < $BACKDIR/fs@snap${this_snap}"

	log_must zfs load-key $POOL/newfs
	log_must zfs mount $POOL/newfs
	actual_cksum=$(recursive_cksum /$POOL/newfs)
	log_must zfs umount $POOL/newfs
	log_must zfs unload-key $POOL/newfs

	if [[ "$expected_cksum" != "$actual_cksum" ]]; then
		log_fail "Checksums differ ($expected_cksum != $actual_cksum)"
	fi

	# Destroy the incremental stream and old snapshot.
	rm -f $BACKDIR/fs@snap${last_snap}
	log_must zfs destroy $POOL/fs@snap${last_snap}
	log_must zfs destroy $POOL/newfs@snap${last_snap}
	last_snap=$this_snap
done

log_pass "Verify encrypted raw incremental receive handles reallocation"
