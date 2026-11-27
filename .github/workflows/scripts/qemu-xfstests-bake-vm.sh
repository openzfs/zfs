#!/usr/bin/env bash

######################################################################
# Bake xfstests into the build VM image.
#
# Runs on the build machine (vm0) AFTER ZFS has been built and installed
# (qemu-4-build.sh, without --poweroff)
# Called on the runner as:
#   ssh zfs@vm0 '$HOME/zfs/.github/workflows/scripts/qemu-xfstests-bake-vm.sh' $OS
#
# The script powers the VM off at the end (like qemu-4-build-vm.sh --poweroff)
######################################################################

set -eu

OS="$1"

# TODO: move the sources under openzfs
XFSTESTS_REPO="${XFSTESTS_REPO:-https://github.com/implr/xfstests}"
XFSTESTS_BRANCH="${XFSTESTS_BRANCH:-zfs}"

echo "##[group]Install xfstests dependencies"
case "$OS" in
  debian*|ubuntu*)
    sudo apt-get update
    # Build toolchain + xfstests build/runtime deps
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
      git build-essential autoconf automake libtool pkg-config gettext \
      uuid-dev libattr1-dev libacl1-dev libaio-dev libgdbm-dev libssl-dev \
      xfsprogs e2fsprogs attr acl quota gdisk parted
    # optional extras for a couple tests
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y fio dbench || true
    ;;
  *)
    echo "xfstests bake is only implemented for debian/ubuntu so far" >&2
    exit 1
    ;;
esac
echo "##[endgroup]"

echo "##[group]Create xfstests service users"
# xfstests insists on these accounts. Digit-leading name
# needs --badnames on modern shadow-utils.
sudo groupadd -f fsgqa
for u in fsgqa fsgqa2 123456-fsgqa; do
  if ! id "$u" &>/dev/null; then
    sudo useradd --badnames -g fsgqa -m "$u" 2>/dev/null \
      || sudo useradd -g fsgqa -m "$u" 2>/dev/null || true
  fi
done

# xfstests runs as root and drops to fsgqa via `su` (common/rc _user_do/_su).
# Installing OpenZFS pulls in libpam-zfs, which registers pam_zfs_key.so in the
# PAM stack to unlock users' encrypted home datasets at login. Its *session*
# hook prompts "Password:" when root su's to fsgqa (to derive the key for the
# nonexistent rpool/home/fsgqa), and that prompt lands in test output and fails
# every _user_do-based test (generic/123, 128, 314, ...). We don't use encrypted
# homes here, so strip pam_zfs_key from the PAM config.
sudo pam-auth-update --package --remove zfs_key 2>/dev/null || true
sudo sed -i '/pam_zfs_key/d' \
  /etc/pam.d/common-auth /etc/pam.d/common-session \
  /etc/pam.d/common-password /etc/pam.d/common-account 2>/dev/null || true
echo "##[endgroup]"

echo "##[group]Clone + build xfstests ($XFSTESTS_BRANCH)"
rm -rf "$HOME/xfstests"
git clone --depth 1 -b "$XFSTESTS_BRANCH" "$XFSTESTS_REPO" "$HOME/xfstests"
cd "$HOME/xfstests"
make -j"$(nproc)"
echo "xfstests baked at $HOME/xfstests ($(git rev-parse --short HEAD))"
echo "##[endgroup]"

# reset cloud-init and power off
sudo cloud-init clean --logs
sync && sleep 2 && sudo poweroff &
exit 0
