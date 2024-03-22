#!/usr/bin/env bash

######################################################################
# 4) configure and build openzfs modules
######################################################################

set -eu
cd $HOME/zfs

function freebsd() {
  echo "##[group]Autogen.sh"
  MAKE="gmake" ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  MAKE="gmake" ./configure \
    --prefix=/usr/local \
    --with-libintl-prefix=/usr/local \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  gmake -j`sysctl -n hw.ncpu` 2>/var/tmp/make-stderr.txt
  echo "##[endgroup]"

  echo "##[group]Install"
  sudo gmake install 2>>/var/tmp/make-stderr.txt
  echo "##[endgroup]"
}

function linux() {
  echo "##[group]Autogen.sh"
  ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  ./configure \
    --prefix=/usr \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  make -j$(nproc) 2>/var/tmp/make-stderr.txt
  echo "##[endgroup]"

  echo "##[group]Install"
  sudo make install 2>>/var/tmp/make-stderr.txt
  echo "##[endgroup]"
}

export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"
case "$1" in
  freebsd*)
    freebsd
    ;;
  *)
    linux
    ;;
esac
