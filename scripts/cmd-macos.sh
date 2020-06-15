#!/bin/bash

if test x"$#" = "x0" ; then
	printf "You need to supply a command.\n"
	exit 1
fi

cmd=$1
shift

READLINK=$(which greadlink 2>/dev/null)
if test "x$READLINK" = "x" ; then
	READLINK=$(which readlink 2>/dev/null)
fi

if ! test "x$READLINK" = "x" ; then
	$READLINK -f . > /dev/null 2>&1
	if ! test x$? = "x0" ; then
		unset READLINK
	else
		CANONICALIZE="$READLINK -f"
	fi
fi

if test "x$READLINK" = "x" ; then
	REALPATH=$(which grealpath 2>/dev/null)
	if test "x$REALPATH" = "x" ; then
		REALPATH=$(which realpath 2>/dev/null)
	fi
	if test "x$REALPATH" = "x" ; then
		CANONICALIZE=readlink
	else
		CANONICALIZE=$REALPATH
	fi
fi

topdir=$(dirname "$($CANONICALIZE "$0")")

if test "x$topdir" = x"." ; then
	if ! test -f zfs.release.in ; then
		printf "cd into the zfs source directory or install GNU readlink or realpath.\n"
		printf "Homebrew: brew install coreutils\n"
		printf "MacPorts: port install coreutils\n"
		printf "Gentoo Prefix: emerge sys-apps/coreutils\n"
		exit 1
	fi
fi

topdir=$topdir/../

for lib in nvpair uutil zpool zfs zfs_core diskmgt; do
	export DYLD_LIBRARY_PATH=$topdir/lib/lib${lib}/.libs:$DYLD_LIBRARY_PATH
done
for c in zdb zfs zpool ztest; do
	export PATH=${topdir}/cmd/${c}/.libs:$PATH
done

#echo PATH=$PATH
#echo DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH
exec "${topdir}/cmd/$cmd/.libs/$cmd" "$@"
