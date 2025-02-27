#!/bin/bash
#
# Wait for a VM to boot up and become active.  This is used in a number of our
# scripts.
#
# $1: VM hostname or IP address

while pidof /usr/bin/qemu-system-x86_64 >/dev/null; do
  ssh 2>/dev/null zfs@$1 "uname -a" && break
done
