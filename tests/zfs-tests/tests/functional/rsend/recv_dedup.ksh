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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verifies that we can receive a dedup send stream by processing it with
# "zstream redup".
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $TESTPOOL/recv "-r"
	rm -r /$TESTPOOL/tar
	rm $sendfile
}
log_onexit cleanup

log_assert "Verify zfs can receive dedup send streams with 'zstream redup'"

typeset sendfile_compressed=$STF_SUITE/tests/functional/rsend/dedup.zsend.bz2
typeset sendfile=/$TESTPOOL/dedup.zsend
typeset tarfile=$STF_SUITE/tests/functional/rsend/fs.tar.gz

log_must eval "bzcat <$sendfile_compressed >$sendfile"
log_must zfs create $TESTPOOL/recv
log_must eval "zstream redup $sendfile | zfs recv -d $TESTPOOL/recv"

log_must mkdir /$TESTPOOL/tar
log_must tar --directory /$TESTPOOL/tar -xzf $tarfile
# The recv'd filesystem is called "/fs", so only compare that subdirectory.
log_must directory_diff /$TESTPOOL/tar/fs /$TESTPOOL/recv/fs

log_pass "zfs can receive dedup send streams with 'zstream redup'"
