#!/bin/bash
set -e
echo "Checking for admin privileges"
if [[ $(id -u) -ne 0 ]]; then
set -e
echo "Please enter your password"
    sudo "$0" "$@"
    exit
fi
set -e
ULOG=`mktemp "$HOME"/zfsuninstaller.XXXXXX`
date >> "$ULOG"
echo "Starting uninstall" | tee -a "$ULOG"
echo "Created uninstall log file...$ULOG" | tee -a "$ULOG"
chmod 777 "$ULOG"
echo "No pools should be imported" | tee -a "$ULOG"

if [ -c /dev/zfs ] ; then
	if [ -x "$(which zpool)" ] ; then
		echo "Checking output of zpool status" | tee -a "$ULOG"
		TMPF=`mktemp /private/tmp/zfsuninstaller-poolcheck.XXXXXX`
		set +e
		2>>"$TMPF" 1>>"$TMPF" zpool status
		err=$?
		set -e
		zpoolstatuslinecount=$(cat "$TMPF" | wc -l | tr -d ' ')
		if [ "$zpoolstatuslinecount" -gt 1 -o "$err" -ne 0 ] ; then
			cat "$TMPF" | tee -a "$ULOG"
			echo ""
			echo "First export all pools" | tee -a "$ULOG"
			echo "Exiting uninstaller" | tee -a "$ULOG"
			echo "Uninstaller log is at $ULOG"
			exit 1
		fi
		rm "$TMPF"
	else
		echo "zpool command does not exist." | tee -a "$ULOG"
		echo "You will need to export your pool(s) and unload zfs.kext manually before uninstalling." | tee -a "$ULOG"
		exit 1
	fi

	if [ -x "$(which zfs)" ] ; then
		echo "Checking output of zfs get name" | tee -a "$ULOG"
		TMPF=`mktemp /private/tmp/zfsuninstaller-zfscheck.XXXXXX`
		set +e
		2>>"$TMPF" 1>>"$TMPF" zfs get name
		err=$?
		set -e
		zfsgetnamelinecount=$(cat "$TMPF" | wc -l)
		if [ "$zfsgetnamelinecount" -gt 0 ] ; then
			cat "$TMPF" | tee -a "$ULOG"
			echo ""
			echo "First export all pools" | tee -a "$ULOG"
			echo "Exiting uninstaller" | tee -a "$ULOG"
			echo "Uninstaller log is at $ULOG"
			exit 1
		fi
		rm "$TMPF"
	else
		echo "zfs command does not exist." | tee -a "$ULOG"
		echo "You will need to export your pool(s) and unload zfs.kext manually before uninstalling." | tee -a "$ULOG"
		exit 1
	fi
fi

echo "Checking if zfs.kext is loaded" | tee -a $ULOG
if [ $(/usr/sbin/kextstat -b org.openzfsonosx.zfs | wc -l) -gt 1 ] ; then
	echo "zfs.kext is loaded" | tee -a "$ULOG"
	echo "Unloading zfs.kext" | tee -a "$ULOG"
set +e
   	sudo /sbin/kextunload -b org.openzfsonosx.zfs
	err=$?
set -e
	if [ "$err" -eq 0 ] ; then
		echo "zfs.kext was unloaded" | tee -a "$ULOG"
	else
		echo "zfs.kext failed to unload with error code $err" | tee -a "$ULOG"
		echo "" | tee -a "$ULOG"
		echo "Continuing uninstall, but it might require a reboot to complete" | tee -a "$ULOG"
	fi
fi

set +e
pgrep zed 1>/dev/null
pgrepret=$?
set -e
if [ $pgrepret -eq 0 ] ; then
	echo "Killing zed" | tee -a "$ULOG"
	killall zed
else
	echo "zed already stopped" | tee -a "$ULOG"
fi

echo "Removing launchd jobs" | tee -a "$ULOG"

set +e
launchctl list | grep org.openzfsonosx.InvariantDisks 1>/dev/null
grepret=$?
set -e
[ $grepret -eq 0 ] && /bin/launchctl remove org.openzfsonosx.InvariantDisks

set +e
launchctl list | grep org.openzfsonosx.zconfigd 1>/dev/null
grepret2=$?
set -e
[ $grepret2 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zconfigd

set +e
launchctl list | grep org.openzfsonosx.zed 1>/dev/null
grepret3=$?
set -e
[ $grepret3 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zed

set +e
launchctl list | grep org.openzfsonosx.zed.service 1>/dev/null
grepret4=$?
set -e
[ $grepret4 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zed.service

set +e
launchctl list | grep org.openzfsonosx.zpool-autoimport 1>/dev/null
grepret5=$?
set -e
[ $grepret5 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zpool-autoimport

set +e
launchctl list | grep org.openzfsonosx.zpool-import-all 1>/dev/null
grepret6=$?
set -e
[ $grepret6 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zpool-import-all

set +e
launchctl list | grep org.openzfsonosx.zpool-import 1>/dev/null
grepret6=$?
set -e
[ $grepret6 -eq 0 ] && /bin/launchctl remove org.openzfsonosx.zpool-import

echo "Deleting installed files" | tee -a "$ULOG"

echo "Removing zfs.kext" | tee -a "$ULOG"
rm -rfv /System/Library/Extensions/zfs.kext
rm -rfv /Library/Extensions/zfs.kext

echo "Rebuilding kextcache" | tee -a "$ULOG"
if [ -x "$(which kextcache)" ] ; then
    kextcache -i / || true
fi

echo "Removing zfs userland from /usr/local/zfs" | tee -a "$ULOG"
rm -rfv /usr/local/zfs

echo "Removing zfs userland from /Library/OpenZFSonOSX" | tee -a "$ULOG"
rm -rfv /Library/OpenZFSonOSX

echo "Removing zfs commands from /usr/sbin" | tee -a "$ULOG"
rm -fv /usr/sbin/InvariantDisks
rm -fv /usr/sbin/zconfigd
rm -fv /usr/sbin/zdb
rm -fv /usr/sbin/zed
rm -fv /usr/sbin/zfs
rm -fv /usr/sbin/zinject
rm -fv /usr/sbin/zpool
rm -fv /usr/sbin/ztest
rm -fv /usr/sbin/zhack
rm -fv /usr/sbin/zpios
rm -fv /usr/sbin/zstreamdump
rm -fv /usr/sbin/zsysctl

echo "Removing zfs commands from /usr/local/bin" | tee -a "$ULOG"
rm -fv /usr/local/bin/arcstat.pl
rm -fv /usr/local/bin/InvariantDisks
rm -fv /usr/local/bin/zconfigd
rm -fv /usr/local/bin/zdb
rm -fv /usr/local/bin/zed
rm -fv /usr/local/bin/zfs
rm -fv /usr/local/bin/zinject
rm -fv /usr/local/bin/zpool
rm -fv /usr/local/bin/ztest
rm -fv /usr/local/bin/zhack
rm -fv /usr/local/bin/zpios
rm -fv /usr/local/bin/zstreamdump
rm -fv /usr/local/bin/zsysctl
rm -fv /usr/local/bin/mount_zfs
rm -fv /usr/local/bin/umount_zfs

echo "Removing zfs libraries from /usr/lib" | tee -a "$ULOG"
rm -fv /usr/lib/libdiskmgt.1.dylib
rm -fv /usr/lib/libdiskmgt.a
rm -fv /usr/lib/libdiskmgt.dylib
rm -fv /usr/lib/libdiskmgt.la
rm -fv /usr/lib/libnvpair.1.dylib
rm -fv /usr/lib/libnvpair.a
rm -fv /usr/lib/libnvpair.dylib
rm -fv /usr/lib/libnvpair.la
rm -fv /usr/lib/libuutil.1.dylib
rm -fv /usr/lib/libuutil.a
rm -fv /usr/lib/libuutil.dylib
rm -fv /usr/lib/libuutil.la
rm -fv /usr/lib/libzfs.2.dylib
rm -fv /usr/lib/libzfs.a
rm -fv /usr/lib/libzfs.dylib
rm -fv /usr/lib/libzfs.la
rm -fv /usr/lib/libzfs_core.1.dylib
rm -fv /usr/lib/libzfs_core.a
rm -fv /usr/lib/libzfs_core.dylib
rm -fv /usr/lib/libzfs_core.la
rm -fv /usr/lib/libzpool.1.dylib
rm -fv /usr/lib/libzpool.a
rm -fv /usr/lib/libzpool.dylib
rm -fv /usr/lib/libzpool.la

echo "Removing zfs libraries from /usr/local/lib" | tee -a "$ULOG"
rm -fv /usr/local/lib/libdiskmgt.1.dylib
rm -fv /usr/local/lib/libdiskmgt.a
rm -fv /usr/local/lib/libdiskmgt.dylib
rm -fv /usr/local/lib/libdiskmgt.la
rm -fv /usr/local/lib/libnvpair.1.dylib
rm -fv /usr/local/lib/libnvpair.a
rm -fv /usr/local/lib/libnvpair.dylib
rm -fv /usr/local/lib/libnvpair.la
rm -fv /usr/local/lib/libuutil.1.dylib
rm -fv /usr/local/lib/libuutil.a
rm -fv /usr/local/lib/libuutil.dylib
rm -fv /usr/local/lib/libuutil.la
rm -fv /usr/local/lib/libzfs.2.dylib
rm -fv /usr/local/lib/libzfs.a
rm -fv /usr/local/lib/libzfs.dylib
rm -fv /usr/local/lib/libzfs.la
rm -fv /usr/local/lib/libzfs_core.1.dylib
rm -fv /usr/local/lib/libzfs_core.a
rm -fv /usr/local/lib/libzfs_core.dylib
rm -fv /usr/local/lib/libzfs_core.la
rm -fv /usr/local/lib/libzpool.1.dylib
rm -fv /usr/local/lib/libzpool.a
rm -fv /usr/local/lib/libzpool.dylib
rm -fv /usr/local/lib/libzpool.la

echo "Removing arcstat.pl from /usr/bin" | tee -a "$ULOG"
rm -fv /usr/bin/arcstat.pl

echo "Removing mount_zfs and umount_zfs from /sbin" | tee -a "$ULOG"
rm -fv /sbin/mount_zfs
rm -fv /sbin/umount_zfs

echo "Removing man pages from /usr/share/man" | tee -a "$ULOG"
rm -fv /usr/share/man/man1/zhack.1
rm -fv /usr/share/man/man1/zpios.1
rm -fv /usr/share/man/man1/ztest.1
rm -fv /usr/share/man/man5/zfs-events.5
rm -fv /usr/share/man/man5/zfs-module-parameters.5
rm -fv /usr/share/man/man5/zpool-features.5
rm -fv /usr/share/man/man8/zdb.8
rm -fv /usr/share/man/man8/zed.8
rm -fv /usr/share/man/man8/zfs.8
rm -fv /usr/share/man/man8/zinject.8
rm -fv /usr/share/man/man8/zpool.8
rm -fv /usr/share/man/man8/zstreamdump.8

echo "Removing man pages from /usr/local/share/man" | tee -a "$ULOG"
rm -fv /usr/local/share/man/man1/zhack.1
rm -fv /usr/local/share/man/man1/zpios.1
rm -fv /usr/local/share/man/man1/ztest.1
rm -fv /usr/local/share/man/man5/zfs-events.5
rm -fv /usr/local/share/man/man5/zfs-module-parameters.5
rm -fv /usr/local/share/man/man5/zpool-features.5
rm -fv /usr/local/share/man/man8/zdb.8
rm -fv /usr/local/share/man/man8/zed.8
rm -fv /usr/local/share/man/man8/zfs.8
rm -fv /usr/local/share/man/man8/zinject.8
rm -fv /usr/local/share/man/man8/zpool.8
rm -fv /usr/local/share/man/man8/zstreamdump.8

echo "Removing /etc/zfs/zed.d" | tee -a "$ULOG"
rm -fv /etc/zfs/zed.d/checksum-email.sh
rm -fv /etc/zfs/zed.d/data-email.sh
rm -fv /etc/zfs/zed.d/generic-email.sh
rm -fv /etc/zfs/zed.d/io-email.sh
rm -fv /etc/zfs/zed.d/resilver.finish-email.sh
rm -fv /etc/zfs/zed.d/scrub.finish-email.sh

rm -fv /etc/zfs/zed.d/all-debug.sh
rm -fv /etc/zfs/zed.d/all-syslog.sh
rm -fv /etc/zfs/zed.d/checksum-notify.sh
rm -fv /etc/zfs/zed.d/checksum-spare.sh
rm -fv /etc/zfs/zed.d/config.remove.sh
rm -fv /etc/zfs/zed.d/config.rename.sh
rm -fv /etc/zfs/zed.d/config.sync.sh
rm -fv /etc/zfs/zed.d/data-notify.sh
rm -fv /etc/zfs/zed.d/generic-notify.sh
rm -fv /etc/zfs/zed.d/io-notify.sh
rm -fv /etc/zfs/zed.d/io-spare.sh
rm -fv /etc/zfs/zed.d/resilver.finish-notify.sh
rm -fv /etc/zfs/zed.d/scrub.finish-notify.sh
rm -fv /etc/zfs/zed.d/snapshot.mount.sh
rm -fv /etc/zfs/zed.d/zed.rc
rm -fv /etc/zfs/zed.d/zpool.destroy.sh
rm -fv /etc/zfs/zed.d/zpool.import.sh
rm -fv /etc/zfs/zed.d/zvol.create.sh
rm -fv /etc/zfs/zed.d/zvol.remove.sh
[ -d  /etc/zfs/zed.d ] && [ $(ls -A /etc/zfs/zed.d | wc -l) -eq 0 ] && rmdir /etc/zfs/zed.d

rm -fv /etc/zfs/zpool.cache.tmp
rm -fv /etc/zfs/zpool.cache
rm -fv /etc/zfs/vdev_id.conf.alias.example
rm -fv /etc/zfs/vdev_id.conf.sas_direct.example
rm -fv /etc/zfs/vdev_id.conf.multipath.example
rm -fv /etc/zfs/vdev_id.conf.sas_switch.example
rm -fv /etc/zfs/zsysctl.conf.example
[ -d  /etc/zfs ] && [ $(ls -A /etc/zfs | wc -l) -eq 0 ] && rmdir /etc/zfs

echo "Removing /usr/libexec/zfs" | tee -a "$ULOG"
rm -rfv /usr/libexec/zfs

echo "Removing /usr/share/zfs" | tee -a "$ULOG"
rm -rfv /usr/share/zfs

echo "Removing /usr/local/libexec/zfs" | tee -a "$ULOG"
rm -rfv /usr/local/libexec/zfs

echo "Removing /usr/local/share/zfs" | tee -a "$ULOG"
rm -rfv /usr/local/share/zfs

echo "Removing launchd plists" | tee -a "$ULOG"
rm -fv /Library/LaunchDaemons/org.openzfsonosx.InvariantDisks.plist
rm -fv /Library/LaunchDaemons/org.openzfsonosx.zconfigd.plist
rm -fv /Library/LaunchDaemons/org.openzfsonosx.zed.plist
rm -fv /Library/LaunchDaemons/org.openzfsonosx.zed.service.plist
rm -fv /Library/LaunchDaemons/org.openzfsonosx.zpool-autoimport.plist
rm -fv /Library/LaunchDaemons/org.openzfsonosx.zpool-import-all.plist

echo "Removing zfs.fs from /System/Library/Filesystems" | tee -a "$ULOG"
rm -rfv /System/Library/Filesystems/zfs.fs

echo "Removing zfs.fs from /Library/Filesystems" | tee -a "$ULOG"
rm -rfv /Library/Filesystems/zfs.fs

echo "Telling OS X to forget zfs packages" | tee -a "$ULOG"
pkgutil --pkgs | grep net | grep openzfsonosx | while read p ; do echo "Forgetting $p" ; pkgutil --forget "$p" ; done

if [ -x "$(which kmutil)" ] ; then
    kmutil clear-staging || true
fi

echo "Exiting uninstaller" | tee -a "$ULOG"
echo "Uninstaller log is at $ULOG"
