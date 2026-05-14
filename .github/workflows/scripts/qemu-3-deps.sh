######################################################################
# 3) Wait for VM to boot from previous step and launch dependencies
#    script on it.
#
# qemu-3-deps.sh [--poweroff] OS_NAME [FEDORA_VERSION]
#
# --poweroff: Power off the VM after installing dependencies
# OS_NAME: OS name (like 'fedora41')
# FEDORA_VERSION: (optional) Experimental Fedora kernel version, like "6.14" to
#     install instead of Fedora defaults.
######################################################################

.github/workflows/scripts/qemu-wait-for-vm.sh vm0

# SPECIAL CASE:
#
# If the user passed in an experimental kernel version to test on Fedora,
# we need to update the kernel version in zfs's META file to allow the
# build to happen.  We update our local copy of META here, since we know
# it will be rsync'd up in the next step.
#
# Look to see if the last argument looks like a kernel version.
ver="${@: -1}"
if [[ $ver =~ ^[0-9]+\.[0-9]+ ]] ; then
  # We got a kernel version, update META to say we support it so we
  # can test against it.
  sed -i -E 's/Linux-Maximum: .+/Linux-Maximum: '$ver'/g' META
fi

scp .github/workflows/scripts/qemu-3-deps-vm.sh zfs@vm0:qemu-3-deps-vm.sh
PID=`pidof /usr/bin/qemu-system-x86_64`
ssh zfs@vm0 '$HOME/qemu-3-deps-vm.sh' "$@"
# wait for poweroff to succeed
tail --pid=$PID -f /dev/null
sleep 5 # avoid this: "error: Domain is already active"
rm -f $HOME/.ssh/known_hosts
