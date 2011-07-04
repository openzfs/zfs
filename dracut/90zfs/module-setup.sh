#!/bin/sh

check() {
  # We depend on udev-rules being loaded
  [ "$1" = "-d" ] && return 0

  # Verify the zfs tool chain
  which zpool >/dev/null 2>&1 || return 1
  which zfs >/dev/null 2>&1 || return 1

  return 0
}

depends() {
  echo udev-rules
  return 0
}

installkernel() {
  instmods zfs
  instmods zcommon
  instmods znvpair
  instmods zavl
  instmods zunicode
  instmods spl
  instmods zlib_deflate
  instmods zlib_inflate
}

install() {
  inst_rules "$moddir/90-zfs.rules"
  inst_rules /etc/udev/rules.d/60-zpool.rules
  inst_rules /etc/udev/rules.d/60-zvol.rules
  inst /etc/zfs/zdev.conf
  inst /etc/zfs/zpool.cache
  inst /etc/hostid
  dracut_install zfs
  dracut_install zpool
  dracut_install zpool_layout
  dracut_install zpool_id
  dracut_install zvol_id
  dracut_install mount.zfs
  dracut_install hostid
  inst_hook cmdline 95 "$moddir/parse-zfs.sh"
  inst_hook mount 98 "$moddir/mount-zfs.sh"
}
