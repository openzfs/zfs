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
    --enable-pyzfs \
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
    --enable-pyzfs \
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

function rpm_build_and_install() {
  EXTRA_CONFIG="${1:-}"
  echo "##[group]Autogen.sh"
  ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
   ./configure --enable-debug --enable-debuginfo $EXTRA_CONFIG
  echo "##[endgroup]"

  echo "##[group]Build"
  make pkg-kmod pkg-utils 2>&1 | tee /var/tmp/make-stderr.txt
  echo "##[endgroup]"

  echo "##[group]Install"
  sudo yum -y localinstall $(ls *.rpm | grep -v src.rpm)
  echo "##[endgroup]"

}

function deb_build_and_install() {
echo "##[group]Autogen.sh"
  ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  ./configure \
    --prefix=/usr \
    --enable-pyzfs \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  make native-deb-kmod native-deb-utils  2>&1 | tee /var/tmp/make-stderr.txt
  echo "##[endgroup]"

  echo "##[group]Install"

  # Do kmod install.  Note that when you build the native debs, the
  # packages themselves are placed in parent directory '../' rather than
  # in the source directory like the rpms are.
  sudo apt-get -y install `find ../ | grep -E '\.deb$' | grep -Ev 'dkms|dracut'`
  echo "##[endgroup]"
}

if [ -e /proc/cmdline ] ; then
    cat /proc/cmdline || true
fi

export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"
case "$1" in
  freebsd*)
    freebsd
    ;;
  alma*|centos*)
    rpm_build_and_install "--with-spec=redhat"
    ;;
  fedora*)
    rpm_build_and_install
    ;;
  debian*|ubuntu*)
    deb_build_and_install
    ;;
  *)
    linux
    ;;
esac
