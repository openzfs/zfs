#!/bin/sh

. /lib/dracut-lib.sh

ZPOOL_FORCE=""

if getargbool 0 zfs_force -y zfs.force -y zfsforce ; then
	warn "ZFS: Will force-import pools if necessary."
	ZPOOL_FORCE="-f"
fi

case "$root" in
	zfs:*)
		# We have ZFS modules loaded, so we're able to import pools now.
		if [ "$root" = "zfs:AUTO" ] ; then
			# Need to parse bootfs attribute
			info "ZFS: Attempting to detect root from imported ZFS pools."

			# Might be imported by the kernel module, so try searching before
			# we import anything.
			zfsbootfs=`zpool list -H -o bootfs | sed -n '/-/ !p' | sed 'q'`
			if [ "$?" != "0" ] || [ "$zfsbootfs" = "" ] || \
				[ "$zfsbootfs" = "no pools available" ] ; then
				# Not there, so we need to import everything.
				info "ZFS: Attempting to import additional pools."
				zpool import -N -a ${ZPOOL_FORCE}
				zfsbootfs=`zpool list -H -o bootfs | sed -n '/-/ !p' | sed 'q'`
				if [ "$?" != "0" ] || [ "$zfsbootfs" = "" ] || \
					[ "$zfsbootfs" = "no pools available" ] ; then
					rootok=0
					pool=""

					warn "ZFS: No bootfs attribute found in importable pools."

					# Re-export everything since we're not prepared to take
					# responsibility for them.
					zpool list -H | while read fs rest ; do
						zpool export "$fs"
					done

					return 1
				fi
			fi
			info "ZFS: Using ${zfsbootfs} as root."
		else
			# Should have an explicit pool set, so just import it and we're done.
			zfsbootfs="${root#zfs:}"
			pool="${zfsbootfs%%/*}"
			if ! zpool list -H $pool > /dev/null 2>&1 ; then
				# pool wasn't imported automatically by the kernel module, so
				# try it manually.
				info "ZFS: Importing pool ${pool}..."
				if ! zpool import -N ${ZPOOL_FORCE} $pool ; then
					warn "ZFS: Unable to import pool ${pool}."
					rootok=0

					return 1
				fi
			fi
		fi

		# Above should have left our rpool imported and pool/dataset in $root.
		# We need zfsutil for non-legacy mounts and not for legacy mounts.
		mountpoint=`zfs get -H -o value mountpoint $zfsbootfs`
		if [ "$mountpoint" = "legacy" ] ; then
			mount -t zfs "$zfsbootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes
		else
			mount -o zfsutil -t zfs "$zfsbootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes
		fi

		# Now we will try to mount /usr early, if there is any /usr partition
		# This will only trigger if /usr is a child file system of the root file system
		zfsusrfs=`zfs list -H -o mountpoint,name | grep '^/usr	' | grep -m 1 -F "	$zfsbootfs" | sed 's|^/usr	||'`
		if [ -n "$zfsusrfs" ] ; then
			# There appears to be a dataset aimed to mount at /usr
			# which is a child of the dataset that goes on /
			if zfs get canmount "$zfsusrfs" | grep -q '  canmount  on' ; then
				mount -o zfsutil -t zfs "$zfsusrfs" "$NEWROOT/usr"
			fi
		fi
		;;
esac
