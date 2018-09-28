#!/bin/sh

#
# Generate zfs_gitrev.h.  Note that we need to do this for every
# invocation of `make`, including for incremental builds.  Therefore we
# can't use a zfs_gitrev.h.in file which would be processed only when
# `configure` is run.
#

file=include/spl/sys/zfs_gitrev.h

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
