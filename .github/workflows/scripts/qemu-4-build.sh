#!/usr/bin/env bash

######################################################################
# 4) configure and build openzfs modules
######################################################################

set -eu

function run() {
  LOG="/var/tmp/build-stderr.txt"
  echo "**************************************************"
  echo "`date` ($*)"
  echo "**************************************************"
  $@ 2>>$LOG
}

function freebsd() {
  export MAKE="gmake"
  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr/local \
    --with-libintl-prefix=/usr/local \
    --enable-pyzfs \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  run gmake -j`sysctl -n hw.ncpu`
  echo "##[endgroup]"

  echo "##[group]Install"
  run sudo gmake install
  echo "##[endgroup]"
}

function linux() {
  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr \
    --enable-pyzfs \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  run make -j$(nproc)
  echo "##[endgroup]"

  echo "##[group]Install"
  run sudo make install
  echo "##[endgroup]"
}

function rpm_build_and_install() {
  EXTRA_CONFIG="${1:-}"
  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure --enable-debug --enable-debuginfo $EXTRA_CONFIG
  echo "##[endgroup]"

  echo "##[group]Build"
  run make pkg-kmod pkg-utils
  echo "##[endgroup]"

  echo "##[group]Install"
  run sudo yum -y --skip-broken localinstall $(ls *.rpm | grep -v src.rpm)
  echo "##[endgroup]"

}

function deb_build_and_install() {
echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr \
    --enable-pyzfs \
    --enable-debug \
    --enable-debuginfo
  echo "##[endgroup]"

  echo "##[group]Build"
  run make native-deb-kmod native-deb-utils
  echo "##[endgroup]"

  echo "##[group]Install"
  # Do kmod install.  Note that when you build the native debs, the
  # packages themselves are placed in parent directory '../' rather than
  # in the source directory like the rpms are.
  run sudo apt-get -y install `find ../ | grep -E '\.deb$' | grep -Ev 'dkms|dracut'`
  echo "##[endgroup]"
}

# Debug: show kernel cmdline
if [ -e /proc/cmdline ] ; then
  cat /proc/cmdline || true
fi

cd $HOME/zfs
export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"

# remove old cloud-init configuration
sudo cloud-init clean --logs
sudo rm -rf /var/lib/cloud \
/etc/systemd/network/10-cloud-init-eth0.network \
/etc/network/interfaces.d/50-cloud-init.cfg \
/etc/netplan/50-cloud-init.yaml

# build
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

sleep 2 && sudo poweroff &
exit 0
