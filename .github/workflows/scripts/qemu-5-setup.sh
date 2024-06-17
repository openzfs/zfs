#!/usr/bin/env bash

######################################################################
# 5) start test machines and load openzfs module
######################################################################

set -eu

# wait for poweroff to succeed
PID=`pidof /usr/bin/qemu-system-x86_64`
tail --pid=$PID -f /dev/null
sudo virsh undefine openzfs

PUBKEY=`cat ~/.ssh/id_ed25519.pub`
OSv=`cat /var/tmp/osvariant.txt`
OS=`cat /var/tmp/os.txt`
VMs=`cat /var/tmp/vms.txt`

CPU="$1"
RAM="$2"
BALLOON="model=none"
#BALLOON="model=virtio,autodeflate=on,freePageReporting=on"
#echo "VMs: ${VMs}x with RAM=$RAM CPU=$CPU BALLOON=$BALLOON"

for i in `seq 1 $VMs`; do
  echo "Generating disk for vm$i ..."
  sudo qemu-img create -q -f qcow2 -F qcow2 \
    -o compression_type=zstd,cluster_size=128k \
    -b /mnt/tests/openzfs.qcow2 "/mnt/tests/vm$i.qcow2"

  cat <<EOF > /tmp/user-data
#cloud-config

fqdn: vm$i

# user:zfs password:1
users:
- name: root
  shell: $BASH
- name: zfs
  sudo: ALL=(ALL) NOPASSWD:ALL
  shell: $BASH
  lock-passwd: false
  passwd: \$1\$EjKAQetN\$O7Tw/rZOHaeBP1AiCliUg/
  ssh_authorized_keys:
    - $PUBKEY

growpart:
  mode: auto
  devices: ['/']
  ignore_growroot_disabled: false
EOF

  sudo virt-install \
    --os-variant $OSv \
    --name "vm$i" \
    --cpu host-passthrough \
    --virt-type=kvm --hvm \
    --vcpus=$CPU,sockets=1 \
    --memory $((1024*RAM)) \
    --memballoon $BALLOON \
    --graphics none \
    --cloud-init user-data=/tmp/user-data \
    --network bridge=virbr0,model=e1000,mac="52:54:00:83:79:0$i" \
    --disk /mnt/tests/vm$i.qcow2,bus=virtio,cache=none,format=qcow2,driver.discard=unmap \
    --import --noautoconsole >/dev/null
done

# check if the machines are okay
echo "Waiting for vm's to come up..."

for i in `seq 1 $VMs`; do
  while true; do
    ssh 2>/dev/null zfs@192.168.122.1$i "uname -a" && break
  done
done

echo "*/2 * * * * sync; echo 1 > /proc/sys/vm/drop_caches" >> crontab.txt
sudo crontab crontab.txt
sudo rm crontab.txt

echo "All $VMs VMs are up now."
