#!/bin/sh

file=include/spl/sys/zfs_gitrev.h

#
# Set default file contents in case we bail.
#
rm -f $file
echo "#define\tZFS_META_GITREV \"unknown\"" >>$file

#
# Check if we are in a git repo.
#
git rev-parse --git-dir > /dev/null 2>&1 || exit

#
# Check if there are uncommitted changes
#
git diff-index --quiet HEAD -- || exit

rev=$(git describe 2>/dev/null)

rm -f $file
echo "#define\tZFS_META_GITREV \"${rev}\"" >>$file
