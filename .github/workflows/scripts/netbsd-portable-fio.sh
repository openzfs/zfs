#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0
#
# pkgsrc fio (cdn.NetBSD.org amd64/11.0) is built with
# -march=znver3 -mavx -mavx2.  QEMU kvm64 and GENERIC without XSAVE
# then SIGILL on the first real instruction.  The binary may also
# omit the mmap ioengine, which mmap_mixed and io/mmap need.
# Rebuild a baseline x86-64 binary with mmap enabled.
set -eu

PREFIX="${PREFIX:-/usr/pkg}"
DEST="${DEST:-$PREFIX/bin/fio}"
WORKDIR="${FIO_BUILDDIR:-/build/tmp/fio-build}"
TARBALL="${FIO_TARBALL:-/build/fio-3.38.tar.gz}"
URL="${FIO_URL:-https://github.com/axboe/fio/archive/refs/tags/fio-3.38.tar.gz}"

fio_has_mmap() {
	[ -x "$1" ] || return 1
	"$1" --version >/dev/null 2>&1 || return 1
	"$1" --enghelp 2>/dev/null | grep -Eqi '^[[:space:]]*mmap[[:space:]]'
}

if fio_has_mmap "$DEST"; then
	echo "fio already portable with mmap: $DEST ($($DEST --version 2>/dev/null | head -1))"
	exit 0
fi

echo "building portable fio with mmap engine -> $DEST"

fetch_tarball() {
	if [ -s "$TARBALL" ]; then
		return 0
	fi
	mkdir -p "$(dirname "$TARBALL")"
	if command -v ftp >/dev/null 2>&1; then
		ftp -V -o "$TARBALL" "$URL" || return 1
	elif command -v curl >/dev/null 2>&1; then
		curl -fsSL -o "$TARBALL" "$URL" || return 1
	else
		return 1
	fi
}

if ! fetch_tarball; then
	echo "ERROR: no fio tarball at $TARBALL and download failed" >&2
	exit 1
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
# GitHub tag tarball unpacks as fio-fio-3.38/; brick.kernel.dk as fio-3.38/.
tar -C "$WORKDIR" -xzf "$TARBALL"
src=$(find "$WORKDIR" -maxdepth 2 -type f -name configure -print | head -1)
src=$(dirname "$src")
cd "$src"

# --disable-native: no -march=native. Extra flags strip AVX from
# a compiler that still defaults to the host bulk-build CPU.
export CFLAGS="-O2 -march=x86-64 -mno-avx -mno-avx2 -mno-sse4.2 -mno-sse4.1 -mno-fma"
if [ -x /usr/pkg/bin/gmake ]; then
	MAKE=/usr/pkg/bin/gmake
else
	MAKE=make
fi
./configure --prefix="$PREFIX" --disable-native
$MAKE -j"${NCPU:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}"
mkdir -p "$(dirname "$DEST")"
# Prefer the just-built binary even if `make install` is noisy.
if [ -x "$src/fio" ]; then
	cp -f "$src/fio" "$DEST"
	chmod 755 "$DEST"
fi
$MAKE install || true

if ! fio_has_mmap "$DEST"; then
	echo "ERROR: portable fio missing mmap ioengine" >&2
	"$DEST" --enghelp 2>&1 | head -40 || true
	exit 1
fi
echo "fio ok with mmap: $($DEST --version 2>/dev/null | head -1)"
