#!/usr/bin/env bash
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
# Build the OpenZFS reproducer in the disposable Debian preparation VM.

set -o errexit
set -o nounset
set -o pipefail

die()
{
	echo "error: $*" >&2
	exit 1
}

# Even validation failures should not leave the disposable VM running until
# the host-side build timeout expires.
trap 'poweroff --force >/dev/null 2>&1 || true' EXIT

(( EUID == 0 )) || die 'must run as root'
(( $# == 2 )) || die 'usage: prepare-zvol-swap-guest.sh SOURCE_DIR STATUS_DIR'
source_dir=$(readlink -e "$1") || die "source directory is unavailable: $1"
status_dir=$2
[[ -d $source_dir ]] || die "source directory is not a directory: $source_dir"
[[ -d $status_dir ]] || die "status directory is not a directory: $status_dir"
findmnt --noheadings --types 9p --target "$source_dir" >/dev/null || \
	die "source directory is not a 9p mount: $source_dir"
findmnt --noheadings --types 9p --target "$status_dir" >/dev/null || \
	die "status directory is not a 9p mount: $status_dir"

status_file=$status_dir/prepare.status
write_status()
{
	local status=$1
	printf '%s\n' "$status" >"$status_file"
}
finish()
{
	local status=$?
	set +o errexit
	if (( status == 0 )); then
		write_status success
	else
		write_status failure
	fi
	# Flush installed modules, tools and service state before the forced
	# poweroff so a successful status cannot accompany a partial image.
	sync
	# This VM has no useful post-build state.  Never leave it running after
	# either a successful preparation or a failed one.
	poweroff --force >/dev/null 2>&1 || true
}
trap finish EXIT

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install --yes --no-install-recommends \
	autoconf automake bash bc bison build-essential ca-certificates \
	coreutils fakeroot flex gawk grep kmod libaio-dev libattr1-dev \
	libblkid-dev libelf-dev libffi-dev libssl-dev libtirpc-dev libtool \
	libudev-dev linux-headers-"$(uname -r)" pkg-config python3 tar \
	udev util-linux uuid-dev zlib1g-dev zstd

kernel_release=$(uname -r)
headers=/usr/src/linux-headers-$kernel_release
[[ -d $headers ]] || die "kernel headers are unavailable: $headers"

build_dir=$(mktemp -d /tmp/zvol-swap-build.XXXXXXXX)
cp -a "$source_dir" "$build_dir/source"
source_copy=$build_dir/source
cd "$source_copy"
./autogen.sh
./configure --prefix=/usr --sysconfdir=/etc --with-config=all \
	--with-linux="$headers" --disable-pyzfs --disable-pam --disable-nls

# Debian splits this assembly macro into the common headers tree.  OpenZFS's
# configure check only searches the architecture-specific build directory.
if ! grep -Fq '#define HAVE_STACK_FRAME_NON_STANDARD_ASM 1' zfs_config.h; then
	for objtool_header in /usr/src/linux-headers-*/include/linux/objtool.h; do
		if [[ -f $objtool_header ]] && \
		    grep -Eq '^[[:space:]]*\.macro[[:space:]]+STACK_FRAME_NON_STANDARD' \
		    "$objtool_header"; then
			echo '#define HAVE_STACK_FRAME_NON_STANDARD_ASM 1' >>zfs_config.h
			break
		fi
	done
fi
make --jobs="$(nproc)"
make install
depmod -a "$kernel_release"

install_dir=/usr/local/libexec/zvol-swap-stall
install -d "$install_dir"
install -m 0755 "$source_copy/contrib/zvol-swap-stall/repro-zvol-swap-stall.sh" \
	"$install_dir/repro-zvol-swap-stall.sh"
install -m 0644 "$source_copy/contrib/zvol-swap-stall/memory-pressure.c" \
	"$install_dir/memory-pressure.c"
cc -std=gnu11 -Wall -Wextra -Werror -O2 -static \
	-o "$install_dir/memory-pressure" "$install_dir/memory-pressure.c"
install -m 0755 "$source_copy/contrib/zvol-swap-stall/zvol-swap-boot.sh" \
	/usr/local/sbin/zvol-swap-boot.sh
install -m 0644 "$source_copy/contrib/zvol-swap-stall/zvol-swap.service" \
	/etc/systemd/system/zvol-swap.service
systemctl daemon-reload
systemctl enable zvol-swap.service

cd /
rm -rf "$build_dir"
apt-get clean
touch /etc/cloud/cloud-init.disabled
