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

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

#
# Generate zfs_gitrev.h.  Note that we need to do this for every
# invocation of `make`, including for incremental builds.  Therefore we
# can't use a zfs_gitrev.h.in file which would be processed only when
# `configure` is run.
#

BASE_DIR=$(dirname "$0")

#file=${BASE_DIR}/../include/spl/sys/zfs_gitrev.h
file=${BASE_DIR}/../include/zfs_gitrev.h

#
# Set default file contents in case we bail.
#
rm -f $file
/bin/echo -e "#define\tZFS_META_GITREV \"unknown\"" >>$file

#
# Check if git is installed and we are in a git repo.
#
git rev-parse --git-dir > /dev/null 2>&1 || exit

#
# Check if there are uncommitted changes
#
git diff-index --quiet HEAD || exit

rev=$(git describe 2>/dev/null) || exit

rm -f $file
/bin/echo -e "#define\tZFS_META_GITREV \"${rev}\"" >>$file
