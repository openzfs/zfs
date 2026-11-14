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
# Copyright (c) 2026, Derzsi Daniel <daniel@tohka.us>
#

#
# DESCRIPTION:
#	Encrypting a large ZIL block must not allocate a large kmem buffer
#	for the AAD.
#
#	The AAD of an encrypted ZIL block grows with the number of records
#	it holds, since each record header is authenticated. Additionally,
#	a single TX_CLONE_RANGE authenticates its whole block pointer array.
#	Likewise, a dnode block authenticates almost the whole block. A ZIL
#	block can therefore hold an AAD larger than 64 KB, the point at
#	which a large allocation warning is emitted, and since the AAD
#	block is always copied into a private buffer, the warning reported
#	in https://github.com/openzfs/zfs/issues/19823 can be emitted
#	during AAD authentication because of the allocation.
#
#	The AAD is mapped in place now instead of copied when possible.
#	This test exists as a regression test to reproduce a large AAD block
#	and validate that the large allocation no longer happens.
#
# STRATEGY:
#	1. Create an encrypted dataset with a small recordsize and write a
#	   file into it.
#	2. Switch the dataset to sync=always and block clone the file. The
#	   clone is logged as a single TX_CLONE_RANGE record.
#	3. Verify that the clone really was a block clone and reads back.
#	4. Verify that no large kmem_alloc() was logged.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset encroot=$TESTPOOL/$TESTFS/enc_aad
typeset encdir=$TESTDIR/enc_aad
typeset bigfile=$encdir/big
typeset clonedfile=$encdir/clone
typeset -i recsize=4096
typeset -i fsize=4 # MiB

#
# At recordsize=4k the file is 1024 block pointers. zil_max_log_data()
# fits about 1000 pointers in one 128K log block. The largest clone
# record therefore has an AAD of about 131000 bytes.
#
typeset -i maxaad=131000

function cleanup
{
	destroy_dataset "$encroot" "-rf"
}

# Number of large kmem_alloc() warnings in the kernel log.
function large_alloc_count
{
	dmesg 2>/dev/null | grep -c "Large kmem_alloc"
}

log_assert "Encrypting a large ZIL block does not allocate a large AAD buffer"
log_onexit cleanup

log_note "This test only exercises the code under test on a build made" \
    "with HAVE_KERNEL_CRYPTO (kernel crypto API backend)"

# The warning is emitted by the SPL and only exists on Linux
if ! is_linux; then
	log_unsupported "spl_kmem_alloc_warn is Linux-specific"
fi

typeset warnfile=/sys/module/spl/parameters/spl_kmem_alloc_warn
if [[ ! -r $warnfile ]]; then
	# The SPL may be built into the zfs module.
	warnfile=/sys/module/zfs/parameters/spl_kmem_alloc_warn
fi
if [[ ! -r $warnfile ]]; then
	log_unsupported "spl does not export spl_kmem_alloc_warn"
fi

typeset -i warnsize=$(cat "$warnfile")
if (( warnsize == 0 )); then
	log_unsupported "spl_kmem_alloc_warn is disabled"
fi

if (( warnsize >= maxaad )); then
	log_unsupported "spl_kmem_alloc_warn ($warnsize) is at least the" \
	    "largest AAD this test can produce ($maxaad)"
fi

# Reading the kernel log is required to observe the warning
if ! dmesg >/dev/null 2>&1; then
	log_unsupported "cannot read the kernel log"
fi

# This test will only produce a large allocation if block cloning is enabled
typeset bclone_state=$(zpool get -H -o value feature@block_cloning \
    "$TESTPOOL")
case $bclone_state in
active|enabled)
	;;
*)
	log_unsupported "block_cloning is $bclone_state on $TESTPOOL"
	;;
esac

log_must eval "echo 'password' | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o keylocation=prompt -o sync=standard" \
    "-o logbias=latency -o recordsize=$recsize -o compression=off" \
    "-o mountpoint=$encdir $encroot"

log_must dd if=/dev/urandom of=$bigfile bs=1M count=$fsize status=none
log_must zpool sync "$TESTPOOL"

typeset -i warn_before bclone_before bclone_after
warn_before=$(large_alloc_count)
bclone_before=$(zpool get -H -p -o value bcloneused "$TESTPOOL")

# Block cloning is only put in the log for synchronous operations
log_must zfs set sync=always $encroot
log_must clonefile -c -q $bigfile $clonedfile
log_must zpool sync "$TESTPOOL"

bclone_after=$(zpool get -H -p -o value bcloneused "$TESTPOOL")
if (( bclone_after <= bclone_before )); then
	log_fail "block cloning did not happen: bcloneused is $bclone_after"
fi

# The encrypted data must still be readable
log_must cmp $bigfile $clonedfile

# If a large allocation has happened, the test has failed
if (( $(large_alloc_count) > warn_before )); then
	log_fail "large kmem_alloc() logged while encrypting a clone record:" \
	    "$(dmesg 2>/dev/null | grep 'Large kmem_alloc' | head -n 5)"
fi

log_pass "Encrypting a large ZIL block does not allocate a large AAD buffer"
