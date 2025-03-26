#!/usr/bin/env bash

######################################################################
# 4) configure and build openzfs modules.  This is run on the VMs.
#
# Usage:
#
#       qemu-4-build-vm.sh OS [--enable-debug][--dkms][--poweroff]
#           [--release][--repo][--tarball]
#
# OS:           OS name like 'fedora41'
# --enable-debug:  Build RPMs with '--enable-debug' (for testing)
# --dkms:       Build DKMS RPMs as well
# --poweroff:   Power-off the VM after building
# --release     Build zfs-release*.rpm as well
# --repo        After building everything, copy RPMs into /tmp/repo
#               in the ZFS RPM repository file structure.  Also
#               copy tarballs if they were built.
# --tarball:    Also build a tarball of ZFS source
######################################################################

ENABLE_DEBUG=""
DKMS=""
POWEROFF=""
RELEASE=""
REPO=""
TARBALL=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --enable-debug)
      ENABLE_DEBUG=1
      shift
      ;;
    --dkms)
      DKMS=1
      shift
      ;;
    --poweroff)
      POWEROFF=1
      shift
      ;;
    --release)
      RELEASE=1
      shift
      ;;
    --repo)
      REPO=1
      shift
      ;;
    --tarball)
      TARBALL=1
      shift
      ;;
    *)
      OS=$1
      shift
      ;;
  esac
done

set -eu

function run() {
  LOG="/var/tmp/build-stderr.txt"
  echo "****************************************************"
  echo "$(date) ($*)"
  echo "****************************************************"
  ($@ || echo $? > /tmp/rv) 3>&1 1>&2 2>&3 | stdbuf -eL -oL tee -a $LOG
  if [ -f /tmp/rv ]; then
    RV=$(cat /tmp/rv)
    echo "****************************************************"
    echo "exit with value=$RV ($*)"
    echo "****************************************************"
    echo 1 > /var/tmp/build-exitcode.txt
    exit $RV
  fi
}

# Look at the RPMs in the current directory and copy/move them to
# /tmp/repo, using the directory structure we use for the ZFS RPM repos.
#
# For example:
# /tmp/repo/epel-testing/9.5
# /tmp/repo/epel-testing/9.5/SRPMS
# /tmp/repo/epel-testing/9.5/SRPMS/zfs-2.3.99-1.el9.src.rpm
# /tmp/repo/epel-testing/9.5/SRPMS/zfs-kmod-2.3.99-1.el9.src.rpm
# /tmp/repo/epel-testing/9.5/kmod
# /tmp/repo/epel-testing/9.5/kmod/x86_64
# /tmp/repo/epel-testing/9.5/kmod/x86_64/debug
# /tmp/repo/epel-testing/9.5/kmod/x86_64/debug/kmod-zfs-debuginfo-2.3.99-1.el9.x86_64.rpm
# /tmp/repo/epel-testing/9.5/kmod/x86_64/debug/libnvpair3-debuginfo-2.3.99-1.el9.x86_64.rpm
# /tmp/repo/epel-testing/9.5/kmod/x86_64/debug/libuutil3-debuginfo-2.3.99-1.el9.x86_64.rpm
# ...
function copy_rpms_to_repo {
  # Pick a RPM to query. It doesn't matter which one - we just want to extract
  # the 'Build Host' value from it.
  rpm=$(ls zfs-*.rpm | head -n 1)

  # Get zfs version '2.2.99'
  zfs_ver=$(rpm -qpi $rpm | awk '/Version/{print $3}')

  # Get "2.1" or "2.2"
  zfs_major=$(echo $zfs_ver | grep -Eo [0-9]+\.[0-9]+)

  # Get 'almalinux9.5' or 'fedora41' type string
  build_host=$(rpm -qpi $rpm | awk '/Build Host/{print $4}')

  # Get '9.5' or '41' OS version
  os_ver=$(echo $build_host | grep -Eo '[0-9\.]+$')

  # Our ZFS version and OS name will determine which repo the RPMs
  # will go in (regular or testing).  Fedora always gets the newest
  # releases, and Alma gets the older releases.
  case $build_host in
  almalinux*)
    case $zfs_major in
    2.2)
      d="epel"
      ;;
    *)
      d="epel-testing"
      ;;
    esac
    ;;
  fedora*)
    d="fedora"
    ;;
  esac

  prefix=/tmp/repo
  dst="$prefix/$d/$os_ver"

  # Special case: move zfs-release*.rpm out of the way first (if we built them).
  # This will make filtering the other RPMs easier.
  mkdir -p $dst
  mv zfs-release*.rpm $dst || true

  # Copy source RPMs
  mkdir -p $dst/SRPMS
  cp $(ls *.src.rpm) $dst/SRPMS/

  if [[ "$build_host" =~ "almalinux" ]] ; then
    # Copy kmods+userspace
    mkdir -p $dst/kmod/x86_64/debug
    cp $(ls *.rpm | grep -Ev 'src.rpm|dkms|debuginfo') $dst/kmod/x86_64
    cp *debuginfo*.rpm $dst/kmod/x86_64/debug
  fi

  if [ -n "$DKMS" ] ; then
    # Copy dkms+userspace
    mkdir -p $dst/x86_64
    cp $(ls *.rpm | grep -Ev 'src.rpm|kmod|debuginfo') $dst/x86_64
  fi

  # Copy debug
  mkdir -p $dst/x86_64/debug
  cp $(ls *debuginfo*.rpm | grep -v kmod) $dst/x86_64/debug
}

function freebsd() {
  extra="${1:-}"

  export MAKE="gmake"
  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr/local \
    --with-libintl-prefix=/usr/local \
    --enable-pyzfs \
    --enable-debuginfo $extra
  echo "##[endgroup]"

  echo "##[group]Build"
  run gmake -j$(sysctl -n hw.ncpu)
  echo "##[endgroup]"

  echo "##[group]Install"
  run sudo gmake install
  echo "##[endgroup]"
}

function linux() {
  extra="${1:-}"

  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr \
    --enable-pyzfs \
    --enable-debuginfo $extra
  echo "##[endgroup]"

  echo "##[group]Build"
  run make -j$(nproc)
  echo "##[endgroup]"

  echo "##[group]Install"
  run sudo make install
  echo "##[endgroup]"
}

function rpm_build_and_install() {
  extra="${1:-}"

  # Build RPMs with XZ compression by default (since gzip decompression is slow)
  echo "%_binary_payload w7.xzdio" >> ~/.rpmmacros

  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure --enable-debuginfo $extra
  echo "##[endgroup]"

  echo "##[group]Build"
  run make pkg-kmod pkg-utils
  echo "##[endgroup]"

  if [ -n "$DKMS" ] ; then
    echo "##[group]DKMS"
    make rpm-dkms
    echo "##[endgroup]"
  fi

  if [ -n "$REPO" ] ; then
    echo "Skipping install since we're only building RPMs and nothing else"
  else
    echo "##[group]Install"
    run sudo dnf -y --nobest install $(ls *.rpm | grep -Ev 'dkms|src.rpm')
    echo "##[endgroup]"
  fi

  # Optionally build the zfs-release.*.rpm
  if [ -n "$RELEASE" ] ; then
    echo "##[group]Release"
    pushd ~
    sudo dnf -y install rpm-build || true
    # Check out a sparse copy of zfsonlinux.github.com.git so we don't get
    # all the binaries.  We just need a few kilobytes of files to build RPMs.
    git clone --depth 1 --no-checkout \
      https://github.com/zfsonlinux/zfsonlinux.github.com.git

    cd zfsonlinux.github.com
    git sparse-checkout set zfs-release
    git checkout
    cd zfs-release

    mkdir -p ~/rpmbuild/{BUILDROOT,SPECS,RPMS,SRPMS,SOURCES,BUILD}
    cp RPM-GPG-KEY-openzfs* *.repo ~/rpmbuild/SOURCES
    cp zfs-release.spec ~/rpmbuild/SPECS/
    rpmbuild -ba ~/rpmbuild/SPECS/zfs-release.spec

    # ZFS release RPMs are built.  Copy them to the ~/zfs directory just to
    # keep all the RPMs in the same place.
    cp ~/rpmbuild/RPMS/noarch/*.rpm ~/zfs
    cp ~/rpmbuild/SRPMS/*.rpm ~/zfs

    popd
    rm -fr ~/rpmbuild
    echo "##[endgroup]"
  fi

  if [ -n "$REPO" ] ; then
    echo "##[group]Repo"
    copy_rpms_to_repo
    echo "##[endgroup]"
  fi
}

function deb_build_and_install() {
  extra="${1:-}"

  echo "##[group]Autogen.sh"
  run ./autogen.sh
  echo "##[endgroup]"

  echo "##[group]Configure"
  run ./configure \
    --prefix=/usr \
    --enable-pyzfs \
    --enable-debuginfo $extra
  echo "##[endgroup]"

  echo "##[group]Build"
  run make native-deb-kmod native-deb-utils
  echo "##[endgroup]"

  echo "##[group]Install"
  # Do kmod install.  Note that when you build the native debs, the
  # packages themselves are placed in parent directory '../' rather than
  # in the source directory like the rpms are.
  run sudo apt-get -y install $(find ../ | grep -E '\.deb$' \
    | grep -Ev 'dkms|dracut')
  echo "##[endgroup]"
}

# Debug: show kernel cmdline
if [ -f /proc/cmdline ] ; then
  cat /proc/cmdline || true
fi

# Set our hostname to our OS name and version number.  Specifically, we set the
# major and minor number so that when we query the Build Host field in the RPMs
# we build, we can see what specific version of Fedora/Almalinux we were using
# to build them.  This is helpful for matching up KMOD versions.
#
# Examples:
#
# rhel8.10
# almalinux9.5
# fedora40
source /etc/os-release
sudo hostname "$ID$VERSION_ID"

# save some sysinfo
uname -a > /var/tmp/uname.txt

cd $HOME/zfs
export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"

extra=""
if [ -n "$ENABLE_DEBUG" ] ; then
  extra="--enable-debug"
fi

# build
case "$OS" in
  freebsd*)
    freebsd "$extra"
    ;;
  alma*|centos*)
    rpm_build_and_install "--with-spec=redhat $extra"
    ;;
  fedora*)
    rpm_build_and_install "$extra"
    ;;
  debian*|ubuntu*)
    deb_build_and_install "$extra"
    ;;
  *)
    linux "$extra"
    ;;
esac

# Optionally build tarballs.  The tarball's root directory name will be named
# after the current tag, like 'zfs-2.3.0' or 'master'.
if [ -n "$TARBALL" ] ; then
  tag="$(git symbolic-ref -q --short HEAD || git describe --tags --exact-match)"
  git archive --format=tar.gz -o $tag.tar.gz $tag
  if [ -n "$REPO" ] ; then
    mkdir -p /tmp/repo/releases
    cp $tag.tar.gz /tmp/repo/releases
  fi
fi

# building the zfs module was ok
echo 0 > /var/tmp/build-exitcode.txt

# reset cloud-init configuration and poweroff
if [ -n "$POWEROFF" ] ; then
        sudo cloud-init clean --logs
        sync && sleep 2 && sudo poweroff &
fi
exit 0
