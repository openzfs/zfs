#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify FIEMAP extent flags are reported correctly.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fiemap/fiemap.kshlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

verify_runnable "both"

function cleanup
{
	rm -f $FIEMAP_FILE $FIEMAP_FILE2
	log_must set_tunable64 metaslab_force_ganging $((2**24 + 1))
}

log_assert "FIEMAP reports all known flags"
log_onexit cleanup

BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

# Verify "encoded" and "data-encrypted" are set on encrypted extents
log_note "Verify 'encoded,data-encrypted' flags"
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o keylocation=prompt" \
    "-o mountpoint=$TESTDIR2 $TESTPOOL/$TESTFS2"
fiemap_write $((1024*1024)) 4 0 $FIEMAP_FILE2
log_must fiemap -sv -F "encoded,data-encrypted:all" $FIEMAP_FILE2
fiemap_remove $FIEMAP_FILE2

# Verify "encoded" is set on compressed extents
log_note "Verify 'encoded' flag for compression"
fiemap_write_compressible $BS 8
fiemap_verify -s -F "encoded:all"
fiemap_remove

# Verify "last" is set on only the last extent
log_note "Verify 'last' flag"
fiemap_write $BS 16
fiemap_verify -s -F "last:=1"
fiemap_remove

# Verify "delalloc" is set only for pending dirty extents
log_note "Verify 'delalloc' flag for pending dirty extents"
fiemap_write $BS 2
fiemap_verify -F "delalloc:all"
fiemap_verify -s -F "delalloc:=0"
fiemap_remove

# Verify "delalloc" is set on pending zeros which will be holes.
log_note "Verify 'delalloc' is set on pending zeros which will be holes"
fiemap_hole $BS 2
fiemap_verify -F "delalloc:all"
fiemap_verify -s -F "delalloc:=0"
fiemap_verify -h -F "unwritten,merged:=1"
fiemap_remove

# Verify "unwritten" is set on holes if they are requested.
log_note "Verify 'unwritten' flag for holes"
fiemap_hole $BS 32
fiemap_verify -s -F "unwritten:0"
fiemap_verify -h -F "unwritten:all"
fiemap_remove

# Verify "merged" is set on blocks merged in to extents.
# Since it's unlikely that all the blocks can be merged due their
# physical offset and device id a large number are written and only
# a single merge is required.
log_note "Verify 'merged' is set on data blocks merged in to extents"
fiemap_write $BS 64
fiemap_verify -s -F "merged:>0"
fiemap_remove

# Verify "merged" is set on holes merged in to extents if requested.
# Holes always merge so there must be only one.
log_note "Verify 'merged' is set on holes merged in to extents"
fiemap_hole $BS 64 0
fiemap_verify -s -F "unwritten:0"
fiemap_verify -h -F "unwritten,merged:=1"
fiemap_remove

# Verify "merged" is set on tail holes if requested.
# Holes always merge so there must be only one.
log_note "Verify 'merged' is set on tail holes"
fiemap_write $BS 32 0
fiemap_hole $BS 32 32
fiemap_verify -s -F "unwritten:0"
fiemap_verify -h -F "unwritten,merged:=1"
fiemap_remove

# Verify "merged" is set on head holes if requested.
# Holes always merge so there must be only one.
log_note "Verify 'merged' is set on head holes"
fiemap_hole $BS 32 0
fiemap_write $BS 32 32
fiemap_verify -s -F "unwritten:0"
fiemap_verify -h -F "unwritten,merged:=1"
fiemap_remove

# Gang blocks
log_note "Verify 'unknown' for gang blocks"
log_must set_tunable64 metaslab_force_ganging $((2**14))
fiemap_write $((2**20)) 64
fiemap_verify -s -F "unknown:>0"
fiemap_remove
log_must set_tunable64 metaslab_force_ganging $((2**24 + 1))

# Embedded blocks
log_note "Verify 'not-aligned,data-inline' for embedding blocks"
fiemap_write_compressible 128 1
fiemap_verify -s -F "not-aligned,data-inline:=1"
fiemap_remove

# Deduplicated blocks
log_note "Verify 'shared' is set for deduplicated blocks"
log_must zfs set dedup=on $TESTPOOL/$TESTFS
fiemap_write $BS 8
log_must cp $FIEMAP_FILE $FIEMAP_FILE_COPY
fiemap_verify -s -F "shared:all"
fiemap_remove $FIEMAP_FILE_COPY
fiemap_remove
log_must zfs set dedup=off $TESTPOOL/$TESTFS

# Verify indirect blocks, they should exist on the device before
# it is removed but afterwards the remapped versions are reported.
log_note "Verify indirect blocks"
fiemap_write $BS 64
fiemap_verify -s -V "0:>0"
log_must zpool remove $TESTPOOL $DISK
wait_for_removal $TESTPOOL
fiemap_verify -s -V "0:=0"
fiemap_remove

log_pass "FIEMAP reports all known flags"
