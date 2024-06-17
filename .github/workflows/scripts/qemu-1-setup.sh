#!/usr/bin/env bash

######################################################################
# 1) setup qemu instance on action runner
######################################################################

set -eu

# install needed packages
sudo apt-get update
sudo apt-get install axel cloud-image-utils daemonize guestfs-tools \
  ksmtuned virt-manager linux-modules-extra-`uname -r`

# generate ssh keys
rm -f ~/.ssh/id_ed25519
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -q -N ""

# no need for some scheduler
for i in /sys/block/s*/queue/scheduler; do
  echo "none" | sudo tee $i > /dev/null
done

# this one is fast and mostly free
sudo mount -o remount,rw,noatime,barrier=0 /mnt

# we expect RAM shortage
cat << EOF | sudo tee /etc/ksmtuned.conf > /dev/null
KSM_MONITOR_INTERVAL=60

# Millisecond sleep between ksm scans for 16Gb server.
# Smaller servers sleep more, bigger sleep less.
KSM_SLEEP_MSEC=10
KSM_NPAGES_BOOST=300
KSM_NPAGES_DECAY=-50
KSM_NPAGES_MIN=64
KSM_NPAGES_MAX=2048

KSM_THRES_COEF=20
KSM_THRES_CONST=2048

LOGFILE=/var/log/ksmtuned.log
DEBUG=1
EOF

sudo systemctl restart ksm
sudo systemctl restart ksmtuned
