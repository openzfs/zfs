#!/bin/sh
# shellcheck disable=SC2016,SC1004,SC2154

grep -wq debug /proc/cmdline && debug=1
[ -n "$debug" ] && echo "zfs-generator: starting" >> /dev/kmsg

GENERATOR_DIR="$1"
[ -n "$GENERATOR_DIR" ] || {
    echo "zfs-generator: no generator directory specified, exiting" >> /dev/kmsg
    exit 1
}

# shellcheck source=zfs-lib.sh.in
. /lib/dracut-zfs-lib.sh
decode_root_args || exit 0

[ -n "$debug" ] && echo "zfs-generator: writing extension for sysroot.mount to $GENERATOR_DIR/sysroot.mount.d/zfs-enhancement.conf" >> /dev/kmsg


mkdir -p "$GENERATOR_DIR"/sysroot.mount.d "$GENERATOR_DIR"/dracut-pre-mount.service.d

{
    echo "[Unit]"
    echo "Before=initrd-root-fs.target"
    echo "After=zfs-import.target"
    echo
    echo "[Mount]"
    echo "PassEnvironment=BOOTFS BOOTFSFLAGS"
    echo 'What=${BOOTFS}'
    echo "Type=zfs"
    echo 'Options=${BOOTFSFLAGS}'
} > "$GENERATOR_DIR"/sysroot.mount.d/zfs-enhancement.conf
ln -fs ../sysroot.mount "$GENERATOR_DIR"/initrd-root-fs.target.requires/sysroot.mount

{
    echo "[Unit]"
    echo "After=zfs-import.target"
} > "$GENERATOR_DIR"/dracut-pre-mount.service.d/zfs-enhancement.conf

[ -n "$debug" ] && echo "zfs-generator: finished" >> /dev/kmsg

exit 0
