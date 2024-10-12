#!/usr/bin/env bash

######################################################################
# 5) start test machines and load openzfs module
######################################################################

set -eu

# read our defined variables
source /var/tmp/env.txt

# wait for poweroff to succeed
PID=$(pidof /usr/bin/qemu-system-x86_64)
tail --pid=$PID -f /dev/null
sudo virsh undefine openzfs

# default values per test vm:
VMs=2
CPU=2

# definitions of per operating system
case "$OS" in
  # FreeBSD can't be optimized via ksmtuned
  freebsd*)
    RAM=6
    ;;
  *)
    # Linux can be optimized via ksmtuned
    RAM=8
    ;;
esac

# this can be different for each distro
echo "VMs=$VMs" >> $ENV

# create snapshot we can clone later
sudo zfs snapshot zpool/openzfs@now

# setup the testing vm's
PUBKEY=$(cat ~/.ssh/id_ed25519.pub)
for i in $(seq 1 $VMs); do

  echo "Creating disk for vm$i..."
  DISK="/dev/zvol/zpool/vm$i"
  FORMAT="raw"
  sudo zfs clone zpool/openzfs@now zpool/vm$i
  sudo zfs create -ps -b 64k -V 80g zpool/vm$i-2

  cat <<EOF > /tmp/user-data
#cloud-config

fqdn: vm$i

users:
- name: root
  shell: $BASH
- name: zfs
  sudo: ALL=(ALL) NOPASSWD:ALL
  shell: $BASH
  ssh_authorized_keys:
    - $PUBKEY

growpart:
  mode: auto
  devices: ['/']
  ignore_growroot_disabled: false
EOF

  sudo virsh net-update default add ip-dhcp-host \
    "<host mac='52:54:00:83:79:0$i' ip='192.168.122.1$i'/>" --live --config

  sudo virt-install \
    --os-variant $OSv \
    --name "vm$i" \
    --cpu host-passthrough \
    --virt-type=kvm --hvm \
    --vcpus=$CPU,sockets=1 \
    --memory $((1024*RAM)) \
    --memballoon model=virtio \
    --graphics none \
    --cloud-init user-data=/tmp/user-data \
    --network bridge=virbr0,model=$NIC,mac="52:54:00:83:79:0$i" \
    --disk $DISK,bus=virtio,cache=none,format=$FORMAT,driver.discard=unmap \
    --disk $DISK-2,bus=virtio,cache=none,format=$FORMAT,driver.discard=unmap \
    --import --noautoconsole >/dev/null
done

# check the memory state from time to time
cat <<EOF > cronjob.sh
# $OS
exec 1>>/var/tmp/stats.txt
exec 2>&1
echo "*******************************************************"
date
uptime
free -m
df -h /mnt/tests
zfs list
EOF
sudo chmod +x cronjob.sh
sudo mv -f cronjob.sh /root/cronjob.sh
echo '*/5 * * * *  /root/cronjob.sh' > crontab.txt
sudo crontab crontab.txt
rm crontab.txt

# check if the machines are okay
echo "Waiting for vm's to come up...  (${VMs}x CPU=$CPU RAM=$RAM)"
for i in $(seq 1 $VMs); do
  while true; do
    ssh 2>/dev/null zfs@192.168.122.1$i "uname -a" && break
  done
done
echo "All $VMs VMs are up now."

# Save the VM's serial output (ttyS0) to /var/tmp/console.txt
# - ttyS0 on the VM corresponds to a local /dev/pty/N entry
# - use 'virsh ttyconsole' to lookup the /dev/pty/N entry
for i in $(seq 1 $VMs); do
  mkdir -p $RESPATH/vm$i
  read "pty" <<< $(sudo virsh ttyconsole vm$i)
  sudo nohup bash -c "cat $pty > $RESPATH/vm$i/console.txt" &
done
echo "Console logging for ${VMs}x $OS started."
