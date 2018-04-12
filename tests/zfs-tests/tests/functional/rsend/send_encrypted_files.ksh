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
# Copyright (c) 2018 by Datto Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#
#
# STRATEGY:
# 1. Create a new encrypted filesystem
# 2. Add an empty file to the filesystem
# 3. Add a small 512 byte file to the filesystem
# 4. Add a larger 32M file to the filesystem
# 5. Add a large sparse file to the filesystem
# 6. Add a file truncated to 4M to the filesystem
# 7. Add a sparse file with metadata compression disabled to the filesystem
# 8. Add and remove 1000 empty files to the filesystem
# 9. Add a file with a large xattr value
# 10. Snapshot the filesystem
# 11. Send and receive the filesystem, ensuring that it can be mounted
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS2
	datasetexists $TESTPOOL/recv && \
		log_must zfs destroy -r $TESTPOOL/recv
	[[ -f $keyfile ]] && log_must rm $keyfile
	[[ -f $sendfile ]] && log_must rm $sendfile
}
log_onexit cleanup

log_assert "Verify 'zfs send -w' works with many different file layouts"

typeset keyfile=/$TESTPOOL/pkey
typeset sendfile=/$TESTPOOL/sendfile

log_must eval "echo 'password' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile $TESTPOOL/$TESTFS2

log_must touch /$TESTPOOL/$TESTFS2/empty
log_must mkfile 512 /$TESTPOOL/$TESTFS2/small
log_must mkfile 32M /$TESTPOOL/$TESTFS2/full
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/sparse \
	bs=512 count=1 seek=10G >/dev/null 2>&1
log_must mkfile 32M /$TESTPOOL/$TESTFS2/truncated
log_must truncate -s 4M /$TESTPOOL/$TESTFS2/truncated
sync

log_must mkdir -p /$TESTPOOL/$TESTFS2/dir
for i in {1..1000}; do
	log_must mkfile 512 /$TESTPOOL/$TESTFS2/dir/file-$i
done
sync

for i in {1..1000}; do
	log_must rm /$TESTPOOL/$TESTFS2/dir/file-$i
done
sync

# ZoL issue #7432
log_must zfs set compression=on xattr=sa $TESTPOOL/$TESTFS2
log_must touch /$TESTPOOL/$TESTFS2/attrs
log_must eval "python -c 'print \"a\" * 4096' | attr -s bigval /$TESTPOOL/$TESTFS2/attrs"

log_must zfs snapshot $TESTPOOL/$TESTFS2@now
log_must eval "zfs send -wR $TESTPOOL/$TESTFS2@now > $sendfile"

log_must eval "zfs recv -F $TESTPOOL/recv < $sendfile"
log_must zfs load-key $TESTPOOL/recv

log_must zfs mount -a

log_pass "Verified 'zfs send -w' works with many different file layouts"
