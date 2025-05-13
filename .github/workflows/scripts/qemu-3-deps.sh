######################################################################
# 3) Wait for VM to boot from previous step and launch dependencies
#    script on it.
#
# $1: OS name (like 'fedora41')
# $2: (optional) Experimental kernel version to install on fedora,
#     like "6.14".
######################################################################

.github/workflows/scripts/qemu-wait-for-vm.sh vm0

# SPECIAL CASE:
#
# If the user passed in an experimental kernel version to test on Fedora,
# we need to update the kernel version in zfs's META file to allow the
# build to happen.  We update our local copy of META here, since we know
# it will be rsync'd up in the next step.
if [ -n "${2:-}" ] ; then
  sed -i -E 's/Linux-Maximum: .+/Linux-Maximum: 99.99/g' META
fi

scp .github/workflows/scripts/qemu-3-deps-vm.sh zfs@vm0:qemu-3-deps-vm.sh
PID=`pidof /usr/bin/qemu-system-x86_64`
ssh zfs@vm0 '$HOME/qemu-3-deps-vm.sh' "$@"
# wait for poweroff to succeed
tail --pid=$PID -f /dev/null
sleep 5 # avoid this: "error: Domain is already active"
rm -f $HOME/.ssh/known_hosts
