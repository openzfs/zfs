#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0
#
# NetBSD 11.0 /bin/dd has no POSIX.1-2024 fullblock.  CURRENT
# (args.c 1.45) added iflags=fullblock.  GNU uses iflag=fullblock
# (singular); NetBSD uses iflags= (plural, open(2) flags stay on
# iflag=).  Build CURRENT dd into PREFIX/bin so ZTS PATH picks it
# up.  Do not replace /bin/dd: cp(1) follows a STF_PATH/dd symlink
# onto the system binary.
set -eu

PREFIX="${PREFIX:-/usr/pkg}"
DEST="${PREFIX}/bin/dd"
WORKDIR="${DD_BUILDDIR:-/build/tmp/netbsd-dd}"
SRCURL="${DD_SRCURL:-https://raw.githubusercontent.com/NetBSD/src/trunk/bin/dd}"

dd_has_iflags_fullblock() {
	[ -x "$1" ] || return 1
	"$1" if=/dev/zero of=/dev/null bs=1 count=1 iflags=fullblock \
		>/dev/null 2>&1
}

if dd_has_iflags_fullblock "$DEST"; then
	echo "dd already has iflags=fullblock: $DEST"
	exit 0
fi

echo "building CURRENT dd -> $DEST"
mkdir -p "$WORKDIR"
files="args.c conv.c conv_tab.c dd.c dd.h dd_swab.c dd_hostops.c extern.h misc.c position.c"
for f in $files; do
	if [ ! -s "$WORKDIR/$f" ]; then
		ftp -V -o "$WORKDIR/$f" "$SRCURL/$f" 2>/dev/null \
			|| curl -fsSL -o "$WORKDIR/$f" "$SRCURL/$f"
	fi
	[ -s "$WORKDIR/$f" ] || {
		echo "ERROR: failed to fetch $f from $SRCURL" >&2
		exit 1
	}
done

cat > "$WORKDIR/Makefile" <<'MAKE'
PROG=	dd
SRCS=	args.c conv.c conv_tab.c dd.c dd_swab.c dd_hostops.c misc.c position.c
CPPFLAGS+=	-D_NETBSD_SOURCE
LDADD+=	-lutil
DPADD+=	${LIBUTIL}
BINDIR=	/usr/pkg/bin
MKMAN=	no
WARNS=	0
.include <bsd.prog.mk>
MAKE

# BINDIR in the makefile is only used by `make install`; copy by hand
# so PREFIX can differ from /usr/pkg.
( cd "$WORKDIR" && make -j"${NCPU:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}" )
[ -x "$WORKDIR/dd" ] || {
	echo "ERROR: CURRENT dd did not build" >&2
	exit 1
}
mkdir -p "$(dirname "$DEST")"
cp -f "$WORKDIR/dd" "$DEST"
chmod 755 "$DEST"

if ! dd_has_iflags_fullblock "$DEST"; then
	echo "ERROR: $DEST rejects iflags=fullblock" >&2
	"$DEST" if=/dev/zero of=/dev/null bs=1 count=1 iflags=fullblock || true
	exit 1
fi
echo "dd ok with iflags=fullblock: $DEST"
ident "$DEST" 2>/dev/null | grep 'args.c' || true
