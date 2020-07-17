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

top_srcdir="$(dirname "$0")/.."
GITREV="${1:-include/zfs_gitrev.h}"

# GITREV should be a relative path (relative to top_builddir)
case "${GITREV}" in
	/*) echo "Error: ${GITREV} should be a relative path" >&2
	    exit 1;;
esac

ZFS_GITREV=$({ cd "${top_srcdir}" &&
	git describe --always --long --dirty 2>/dev/null; } || :)
ZFS_GITREV=${ZFS_GITREV:-unknown}

GITREVTMP="${GITREV}~"
printf '#define\tZFS_META_GITREV "%s"\n' "${ZFS_GITREV}" >"${GITREVTMP}"
if cmp -s "${GITREV}" "${GITREVTMP}"
then
	rm -f "${GITREVTMP}"
else
	mv -f "${GITREVTMP}" "${GITREV}"
fi
