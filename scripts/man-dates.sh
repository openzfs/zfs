#!/bin/sh

# This script updates the date lines in the man pages to the date of the last
# commit to that file.

set -eu

find man -type f | while read -r i ; do
    git_date=$(git log -1 --date=short --format="%ad" -- "$i")
    [ "x$git_date" = "x" ] && continue
    sed -i "s|^\.Dd.*|.Dd $(date -d "$git_date" "+%B %-d, %Y")|" "$i"
done
