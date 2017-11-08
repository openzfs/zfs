#!/bin/bash

# This script only gets executed on systemd systems, see mount-zfs.sh for non-systemd systems

# import the libs now that we know the pool imported
[ -f /lib/dracut-lib.sh ] && dracutlib=/lib/dracut-lib.sh
[ -f /usr/lib/dracut/modules.d/99base/dracut-lib.sh ] && dracutlib=/usr/lib/dracut/modules.d/99base/dracut-lib.sh
. "$dracutlib"

# load the kernel command line vars
[ -z "$root" ] && root=$(getarg root=)
# If root is not ZFS= or zfs: or rootfstype is not zfs then we are not supposed to handle it.
[ "${root##zfs:}" = "${root}" -a "${root##ZFS=}" = "${root}" -a "$rootfstype" != "zfs" ] && exit 0

# There is a race between the zpool import and the pre-mount hooks, so we wait for a pool to be imported
while true; do
    zpool list -H | grep -q -v '^$' && break
    [[ $(systemctl is-failed zfs-import-cache.service) == 'failed' ]] && exit 1
    [[ $(systemctl is-failed zfs-import-scan.service) == 'failed' ]] && exit 1
    sleep 0.1s
done

# run this after import as zfs-import-cache/scan service is confirmed good
if [[ "${root}" = "zfs:AUTO" ]] ; then
    root=$(zpool list -H -o bootfs | awk '$1 != "-" {print; exit}')
else
    root="${root##zfs:}"
    root="${root##ZFS=}"
fi

# if pool encryption is active and the zfs command understands '-o encryption'
if [[ $(zpool list -H -o feature@encryption $(echo "${root}" | awk -F\/ '{print $1}')) == 'active' ]]; then
    # check if root dataset has encryption enabled
    if $(zfs list -H -o encryption "${root}" | grep -q -v off); then
        # figure out where the root dataset has its key, the keylocation should not be none
        while true; do
            if [[ $(zfs list -H -o keylocation "${root}") == 'none' ]]; then
                root=$(echo -n "${root}" | awk 'BEGIN{FS=OFS="/"}{NF--; print}')
                [[ "${root}" == '' ]] && exit 1
            else
                break
            fi
        done
        # decrypt them
        TRY_COUNT=5
        while [ $TRY_COUNT != 0 ]; do
            zfs load-key "$root" <<< $(systemd-ask-password "Encrypted ZFS password for ${root}: ")
            [[ $? == 0 ]] && break
            ((TRY_COUNT-=1))
        done
    fi
fi
