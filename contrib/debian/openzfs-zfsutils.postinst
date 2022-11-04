#!/bin/sh
set -e

# When processed to here but zfs kernel module is not loaded, the subsequent
# services would fail to start. In this case the installation process just
# fails at the postinst stage. The user could do
#   $ sudo modprobe zfs; sudo dpkg --configure -a
# to complete the installation.
#
modprobe -v zfs || true # modprobe zfs does nothing if zfs.ko was already loaded.

#DEBHELPER#

