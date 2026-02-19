#!/usr/bin/env bash

######################################################################
# 6) load openzfs module and run the tests
#
# called on runner:  qemu-6-tests.sh
# called on qemu-vm: qemu-6-tests.sh $OS $2 $3 [--lustre|--builtin] [quick|default]
#
# --lustre: Test build lustre in addition to the normal tests
# --builtin: Test build ZFS as a kernel built-in in addition to the normal tests
######################################################################

set -eu

function prefix() {
  ID="$1"
  LINE="$2"
  CURRENT=$(date +%s)
  TSSTART=$(cat /tmp/tsstart)
  DIFF=$((CURRENT-TSSTART))
  H=$((DIFF/3600))
  DIFF=$((DIFF-(H*3600)))
  M=$((DIFF/60))
  S=$((DIFF-(M*60)))

  CTR=$(cat /tmp/ctr)
  echo $LINE| grep -q '^\[.*] Test[: ]' && CTR=$((CTR+1)) && echo $CTR > /tmp/ctr

  BASE="$HOME/work/zfs/zfs"
  COLOR="$BASE/scripts/zfs-tests-color.sh"
  CLINE=$(echo $LINE| grep '^\[.*] Test[: ]' \
    | sed -e 's|^\[.*] Test|Test|g' \
    | sed -e 's|/usr/local|/usr|g' \
    | sed -e 's| /usr/share/zfs/zfs-tests/tests/| |g' | $COLOR)
  if [ -z "$CLINE" ]; then
    printf "vm${ID}: %s\n" "$LINE"
  else
    # [vm2: 00:15:54  256] Test: functional/checksum/setup (run as root) [00:00] [PASS]
    printf "[vm${ID}: %02d:%02d:%02d %4d] %s\n" \
      "$H" "$M" "$S" "$CTR" "$CLINE"
  fi
}

function do_lustre_build() {
  local rc=0
  $HOME/zfs/.github/workflows/scripts/qemu-6-lustre-tests-vm.sh &> /var/tmp/lustre.txt || rc=$?
  echo "$rc" > /var/tmp/lustre-exitcode.txt
  if [ "$rc" != "0" ] ; then
      echo "$rc" > /var/tmp/tests-exitcode.txt
  fi
}
export -f do_lustre_build

# Test build ZFS into the kernel directly
function do_builtin_build() {
  local rc=0
  # Get currently full kernel version (like '6.18.8')
  fullver=$(uname -r | grep -Eo '^[0-9]+\.[0-9]+\.[0-9]+')

  # Get just the major ('6')
  major=$(echo $fullver | grep -Eo '^[0-9]+')
  (
  set -e

  wget https://cdn.kernel.org/pub/linux/kernel/v${major}.x/linux-$fullver.tar.xz
  tar -xf $HOME/linux-$fullver.tar.xz
  cd $HOME/linux-$fullver
  make tinyconfig
  ./scripts/config --enable EFI_PARTITON
  ./scripts/config --enable BLOCK
  # BTRFS_FS is easiest config option to enable CONFIG_ZLIB_INFLATE|DEFLATE
  ./scripts/config --enable BTRFS_FS
  yes "" | make oldconfig
  make prepare

  cd $HOME/zfs
  ./configure --with-linux=$HOME/linux-$fullver --enable-linux-builtin --enable-debug
  ./copy-builtin $HOME/linux-$fullver

  cd $HOME/linux-$fullver
  ./scripts/config --enable ZFS
  yes "" | make oldconfig
  make -j `nproc`
  ) &> /var/tmp/builtin.txt || rc=$?
  echo "$rc" > /var/tmp/builtin-exitcode.txt
  if [ "$rc" != "0" ] ; then
      echo "$rc" > /var/tmp/tests-exitcode.txt
  fi
}
export -f do_builtin_build

# called directly on the runner
if [ -z ${1:-} ]; then
  cd "/var/tmp"
  source env.txt
  SSH=$(which ssh)
  TESTS='$HOME/zfs/.github/workflows/scripts/qemu-6-tests.sh'
  echo 0 > /tmp/ctr
  date "+%s" > /tmp/tsstart

  for ((i=1; i<=VMs; i++)); do
    IP="192.168.122.1$i"

    # We do an additional test build of Lustre against ZFS if we're vm2
    # on almalinux*.  At the time of writing, the vm2 tests were
    # completing roughly 15min before the vm1 tests, so it makes sense
    # to have vm2 do the build.
    #
    # In addition, we do an additional test build of ZFS as a Linux
    # kernel built-in on Fedora.  Again, we do it on vm2 to exploit vm2's
    # early finish time.
    extra=""
    if [[ "$OS" == almalinux* ]] && [[ "$i" == "2" ]] ; then
        extra="--lustre"
    elif [[ "$OS" == fedora* ]] && [[ "$i" == "2" ]] ; then
        extra="--builtin"
    fi

    daemonize -c /var/tmp -p vm${i}.pid -o vm${i}log.txt -- \
      $SSH zfs@$IP $TESTS $OS $i $VMs $extra $CI_TYPE
    # handly line by line and add info prefix
    stdbuf -oL tail -fq vm${i}log.txt \
      | while read -r line; do prefix "$i" "$line"; done &
    echo $! > vm${i}log.pid
    # don't mix up the initial --- Configuration --- part
    sleep 0.13
  done

  # wait for all vm's to finish
  for ((i=1; i<=VMs; i++)); do
    tail --pid=$(cat vm${i}.pid) -f /dev/null
    pid=$(cat vm${i}log.pid)
    rm -f vm${i}log.pid
    kill $pid
  done

  exit 0
fi


#############################################
# Everything from here on runs inside qemu vm
#############################################

# Process cmd line args
OS="$1"
shift
NUM="$1"
shift
DEN="$1"
shift

BUILD_LUSTRE=0
BUILD_BUILTIN=0
if [ "$1" == "--lustre" ] ; then
  BUILD_LUSTRE=1
  shift
elif [ "$1" == "--builtin" ] ; then
  BUILD_BUILTIN=1
  shift
fi

if [ "$1" == "quick" ] ; then
  export RUNFILES="sanity.run"
fi

export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
case "$OS" in
  freebsd*)
    TDIR="/usr/local/share/zfs"
    sudo kldstat -n zfs 2>/dev/null && sudo kldunload zfs
    sudo -E ./zfs/scripts/zfs.sh
    sudo mv -f /var/tmp/*.txt /tmp
    sudo newfs -U -t -L tmp /dev/vtbd1 >/dev/null
    sudo mount -o noatime /dev/vtbd1 /var/tmp
    sudo chmod 1777 /var/tmp
    sudo mv -f /tmp/*.txt /var/tmp
    ;;
  *)
    # use xfs @ /var/tmp for all distros
    TDIR="/usr/share/zfs"
    sudo -E modprobe zfs
    sudo mv -f /var/tmp/*.txt /tmp
    sudo mkfs.xfs -fq /dev/vdb
    sudo mount -o noatime /dev/vdb /var/tmp
    sudo chmod 1777 /var/tmp
    sudo mv -f /tmp/*.txt /var/tmp
    ;;
esac

# Distribution-specific settings.
case "$OS" in
  almalinux9|almalinux10|centos-stream*)
    # Enable io_uring on Enterprise Linux 9 and 10.
    sudo sysctl kernel.io_uring_disabled=0 > /dev/null
    ;;
  alpine*)
    # Ensure `/etc/zfs/zpool.cache` exists.
    sudo mkdir -p /etc/zfs
    sudo touch /etc/zfs/zpool.cache
    sudo chmod 644 /etc/zfs/zpool.cache
    ;;
esac

# Lustre calls a number of exported ZFS module symbols.  To make sure we don't
# change the symbols and break Lustre, do a quick Lustre build of the latest
# released Lustre against ZFS.
#
# Note that we do the Lustre test build in parallel with ZTS.  ZTS isn't very
# CPU intensive, so we can use idle CPU cycles "guilt free" for the build.
# The Lustre build on its own takes ~15min.
if [ "$BUILD_LUSTRE" == "1" ] ; then
  do_lustre_build &
elif [ "$BUILD_BUILTIN" == "1" ] ; then
  # Try building ZFS directly into the Linux kernel (not as a module)
  do_builtin_build &
fi

# run functional testings and save exitcode
cd /var/tmp
TAGS=$NUM/$DEN
sudo dmesg -c > dmesg-prerun.txt
mount > mount.txt
df -h > df-prerun.txt
$TDIR/zfs-tests.sh -vKO -s 3GB -T $TAGS

RV=$?
df -h > df-postrun.txt
echo $RV > tests-exitcode.txt
sync
exit 0
