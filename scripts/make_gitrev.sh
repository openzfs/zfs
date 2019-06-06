#!/bin/sh

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

# Copyright (c) 2018 by Delphix. All rights reserved.
# Copyright (c) 2018 by Matthew Thode. All rights reserved.

#
# Generate zfs_gitrev.h.  Note that we need to do this for every
# invocation of `make`, including for incremental builds.  Therefore we
# can't use a zfs_gitrev.h.in file which would be processed only when
# `configure` is run.
#

set -e -u

cleanup() {
    ZFS_GIT_REV=${ZFS_GIT_REV:-"unknown"}
    cat << EOF > "$(dirname "$0")"/../include/zfs_gitrev.h
#define	ZFS_META_GITREV "${ZFS_GIT_REV}"
EOF
}
trap cleanup EXIT

# Check if git is installed and we are in a git repo.
git rev-parse --git-dir > /dev/null 2>&1
# Get the git current git revision
ZFS_GIT_REV=$(git describe --always --long --dirty 2>/dev/null)
# Check if header file already contain the exact string
grep -sq "\"${ZFS_GIT_REV}\"" "$(dirname "$0")"/../include/zfs_gitrev.h &&
	trap - EXIT
exit 0
