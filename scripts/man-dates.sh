#!/bin/sh

# This script updates the date lines in the man pages to the date of the last
# commit to that file.

set -eu

update=0
[ "${1-}" = "--update" ] && update=1

find man -type f | while read -r i ; do
    git_date=$(git log -1 --date=short --format="%ad" -- "$i")
    [ "x$git_date" = "x" ] && continue
    if [ "$update" = "1" ] ; then
        sed -i "s|^\.Dd.*|.Dd $(date -d "$git_date" "+%B %-d, %Y")|" "$i"
    else
        sed "s|^\.Dd.*|.Dd $(date -d "$git_date" "+%B %-d, %Y")|" "$i" | \
            diff -u "$i" -
    fi
done
