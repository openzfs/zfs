#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
# Copyright (c) 2017 by Fan Yong. All rights reserved.
# Copyright 2022 Zettabyte Software, LLC.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify FS_CASEFOLD_FL is unsupported on case sensitive filesystems.
#
# STRATEGY:
#	1. Create a filesystem with casesensitivity=sensitive.
#	2. Create directory in filesystem and make a file in it.
#	3. Use lsattr to verify '-F' is set on the directory.
#	4. Use lsattr to verify '-F' is set on the file.
#	5. Use chattr to verify setting '+F' on the directory is rejected.
#	6. Use chattr to verify setting '+F' on the file is rejected.
#	7. Use lsattr to verify F has not changed on either.
#	8. Use chattr to verify setting '-F' on the directory is accepted.
#	9. Use chattr to verify setting '-F' on the file is accepted.
#	10. Use lsattr to verify F has not changed on either.
#	11. Repeat steps 3 through 10 as a user that owns both.
#

TESTFS=$TESTPOOL/chattr_004_neg

function cleanup
{
	datasetexists $TESTFS && destroy_dataset $TESTFS
}

log_assert "Check whether chattr +F is unsupported as expected on case " \
	"sensitive filesystems."

e2ver=$(lsattr -Vd 3>&1 1>/dev/null 2>&3-)
e2ver=${e2ver% *}
e2ver=${e2ver#* }
major="${e2ver%%.*}"
minor="${e2ver#*.*}"
minor="${minor%%.*}"

if test $major -lt 1 -o $major -eq 1 -a $minor -lt 45; then
	log_unsupported "This test requires e2fsprogs version 1.45.0 or " \
	"later. Saw version $e2ver, skipping."
fi

log_onexit cleanup

log_must zfs create -o casesensitivity=sensitive $TESTFS

log_must mkdir /$TESTFS/dir

log_must mkfile 16M /$TESTFS/dir/file

log_must eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_mustnot chattr +F /$TESTFS/dir
log_mustnot chattr +F /$TESTFS/dir/file

log_must eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_must chattr -F /$TESTFS/dir
log_must chattr -F /$TESTFS/dir/file

log_must eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_must chmod -R 0777 /$TESTFS/dir
log_must chown -R $QUSER1 /$TESTFS/dir

log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_mustnot user_run $QUSER1 chattr +F /$TESTFS/dir
log_mustnot user_run $QUSER1 chattr +F /$TESTFS/dir/file

log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_must user_run $QUSER1 chattr -F /$TESTFS/dir
log_must user_run $QUSER1 chattr -F /$TESTFS/dir/file

log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir | grep -v '\-F[- ]* '"
log_must user_run $QUSER1 eval "lsattr -d /$TESTFS/dir/file | grep -v '\-F[- ]* '"

log_pass "chattr +F is unsupported on case sensitive filesystems."
