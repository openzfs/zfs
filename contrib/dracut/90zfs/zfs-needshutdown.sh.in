#!/bin/sh

command -v getarg >/dev/null 2>&1 || . /lib/dracut-lib.sh

if [ -z "$(zpool get -Ho value name)" ]; then
    info "ZFS: No active pools, no need to export anything."
else
    info "ZFS: There is an active pool, will export it."
    need_shutdown
fi
