#!/bin/sh

_do_zpool_export() {
	info "ZFS: Exporting ZFS storage pools..."
	errs=$(zpool export -aF 2>&1)
	ret=$?
	echo "${errs}" | vwarn
	if [ "${ret}" -ne 0 ]; then
		info "ZFS: There was a problem exporting pools."
	fi

	if [ -n "$1" ]; then
		info "ZFS: pool list"
		zpool list 2>&1 | vinfo
	fi

	return "$ret"
}

if command -v zpool >/dev/null; then
	_do_zpool_export "${1}"
fi
