#!/usr/bin/env bash

######################################################################
# 5) start test machines and load openzfs module
######################################################################

set -eu

# machine not needed anymore
while pidof /usr/bin/qemu-system-x86_64 >/dev/null; do sleep 1; done
sudo virsh undefine openzfs

###############################################################
# 1) 4GB RAM for host
# 2) 3x 4GB RAM for qemu
###############################################################
PUBKEY=`cat ~/.ssh/id_ed25519.pub`
OSv=`cat /var/tmp/osvariant.txt`
OS=`cat /var/tmp/os.txt`
for i in `seq 1 3`; do

  if [ $i = 1 ]; then
    # 8 vdisks on /mnt
    BASE="/mnt"
  else
    # 4 vdisks on /
    BASE="/mnt2"
  fi

  OPTS="-q -f qcow2 -o compression_type=zstd,preallocation=off"
  SIZE="2"

  echo "Generating vm$i with 3x${SIZE}GiB disks."
  sudo mkdir -p $BASE
  sudo qemu-img create $OPTS -b /mnt/openzfs.qcow2 -F qcow2 "$BASE/vm$i.qcow2"
  sudo qemu-img create $OPTS "$BASE/vm$i-1.qcow2" ${SIZE}G
  sudo qemu-img create $OPTS "$BASE/vm$i-2.qcow2" ${SIZE}G
  sudo qemu-img create $OPTS "$BASE/vm$i-3.qcow2" ${SIZE}G

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
    --vcpus=3,sockets=1 \
    --memory $((1024*4)) \
    --memballoon model=none \
    --graphics none \
    --cloud-init user-data=/tmp/user-data \
    --network bridge=virbr0,model=e1000,mac="52:54:00:83:79:0$i" \
    --controller=scsi,model=virtio-scsi \
    --disk "$BASE/vm$i.qcow2",format=qcow2,bus=scsi,driver.discard=unmap \
    --disk "$BASE/vm$i-1.qcow2",format=qcow2,bus=scsi,driver.discard=unmap \
    --disk "$BASE/vm$i-2.qcow2",format=qcow2,bus=scsi,driver.discard=unmap \
    --disk "$BASE/vm$i-3.qcow2",format=qcow2,bus=scsi,driver.discard=unmap \
    --import --noautoconsole >/dev/null
done

# check if the machines are okay
echo "Waiting for vm's to come up..."
while true; do ssh 2>/dev/null zfs@192.168.122.11 "uname -a" && break; done
while true; do ssh 2>/dev/null zfs@192.168.122.12 "uname -a" && break; done
while true; do ssh 2>/dev/null zfs@192.168.122.13 "uname -a" && break; done
