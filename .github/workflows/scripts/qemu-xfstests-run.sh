#!/usr/bin/env bash

######################################################################
# Run xfstests
#
#   - called with 'guest'  -> runs inside the test VM (vm1)
# called on runner:  qemu-xfstests-run.sh
# called on qemu-vm: qemu-xfstests-run.sh guest
#
######################################################################

set -eu

############################
# Runner side (no args)
############################
if [ -z "${1:-}" ]; then
  source /var/tmp/env.txt
  SCRIPT='$HOME/zfs/.github/workflows/scripts/qemu-xfstests-run.sh'
  # Pass the user ./check arguments through to the guest.
  ssh zfs@vm1 "OS='$OS' XFSTESTS_OPTIONS='${XFSTESTS_OPTIONS:-}' $SCRIPT guest" \
    2>&1 | stdbuf -oL tee /var/tmp/xfstests-run.log
  exit "${PIPESTATUS[0]}"
fi

############################
# VM side
############################
OS="${OS:?}"
OPTS="${XFSTESTS_OPTIONS:--g quick}"
export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin:/usr/local/bin"

sudo -E modprobe zfs

# Longer RCU timeouts (as in qemu-6-tests.sh)
rcu="/sys/module/rcupdate/parameters/rcu_cpu_stall_timeout"
test -f "$rcu" && echo 120 | sudo tee "$rcu" >/dev/null || true

# Carve the 64 GiB tests disk (attached by qemu-5-setup.sh as the 2nd virtio
# disk) into a TEST_DEV partition + three SCRATCH pool members.
DEV=/dev/vdb
sudo wipefs -a "$DEV" || true
sudo sgdisk -Z "$DEV"
sudo sgdisk -n1:0:+20G -n2:0:+14G -n3:0:+14G -n4:0:0 "$DEV"
sudo partprobe "$DEV"
sleep 2

# Persistent test pool/dataset.
# (acltype=posix + xattr=sa are required for several generic/ tests).
sudo zpool create -f \
  -O mountpoint=legacy -O acltype=posix -O xattr=sa \
  -O compression=off -O relatime=off \
  testpool "${DEV}1"
sudo zfs create -o mountpoint=legacy -o recordsize=64K testpool/testfs

sudo mkdir -p /mnt/test /mnt/scratch
sudo mount -t zfs testpool/testfs /mnt/test

cd "$HOME/xfstests"
cat > local.config <<EOF
export FSTYP=zfs
export TEST_DEV=testpool/testfs
export TEST_DIR=/mnt/test
export SCRATCH_MNT=/mnt/scratch
export SCRATCH_ZPOOL_NAME=scratchpool
export SCRATCH_DEV_POOL="${DEV}2 ${DEV}3 ${DEV}4"
EOF

# Apply the ZFS exclude list only for group runs; when a caller names tests
# explicitly they want exactly those (e.g. debugging an excluded failure).
EXCLUDE=""
case "$OPTS" in
  *-g*) EXCLUDE="-E exclude.zfs.txt" ;;
esac

sudo dmesg -c > /var/tmp/dmesg-prerun.txt || true

RV=0
# shellcheck disable=SC2086 # word-splitting of OPTS/EXCLUDE is intentional
sudo HOST_OPTIONS="$PWD/local.config" ./check $EXCLUDE $OPTS || RV=$?
echo "$RV" | sudo tee /var/tmp/tests-exitcode.txt >/dev/null

sudo dmesg > /var/tmp/dmesg-postrun.txt || true
sync
exit "$RV"
