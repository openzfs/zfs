#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2023, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zdb -K ...' should enable reading from a raw-encrypted dataset
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Write some data to a file
# 3. Run zdb -dddd on the file, confirm it can't be read
# 4. Run zdb -K ... -ddddd on the file, confirm it can be read
#

verify_runnable "both"

dataset="$TESTPOOL/$TESTFS2"
file="$TESTDIR2/somefile"
keyfile="$TEST_BASE_DIR/keyfile"

function cleanup
{
	datasetexists "$dataset" && destroy_dataset "$dataset" -f
	rm -f "$keyfile"
	default_cleanup_noexit
}

log_onexit cleanup

log_must default_setup_noexit $DISKS

log_assert "'zdb -K' should enable reading from a raw-encrypted dataset"

# The key must be 32 bytes long.
echo -n "$RAWKEY" > "$keyfile"

log_must zfs create -o mountpoint="$TESTDIR2" \
    -o encryption=on -o keyformat=raw -o keylocation="file://$keyfile" \
    "$dataset"

echo 'my great encrypted text' > "$file"

typeset -i obj=$(ls -i "$file" | cut -d' ' -f1)
typeset -i size=$(wc -c < "$file")

log_note "test file $file is objid $obj, size $size"

sync_pool "$TESTPOOL" true

log_must eval "zdb -dddd $dataset $obj | grep -q 'object encrypted'"

log_must eval "zdb -K $keyfile -dddd $dataset $obj | grep -q 'size\s$size$'"

log_pass "'zdb -K' enables reading from a raw-encrypted dataset"
