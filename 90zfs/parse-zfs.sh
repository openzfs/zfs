case "$root" in
    zfs:FILESYSTEM=*|FILESYSTEM=*)
	root="${root#zfs:}"
	root="$(echo $root | sed 's,/,\\x2f,g')"
	root="zfs:${root#FILESYSTEM=}"
        rootfstype="zfs"
        rootok=1 ;;
    zfs:ZFS=*|ZFS=*)
	root="${root#zfs:}"
	root="$(echo $root | sed 's,/,\\x2f,g')"
	root="zfs:${root#ZFS=}"
        rootfstype="zfs"
        rootok=1 ;;
esac
