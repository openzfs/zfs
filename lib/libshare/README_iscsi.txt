This is iSCSI support for the IET iSCSI targets.

It will call ietmadm to both add or remove a iSCSI
target from the call to 'zfs share':

       zfs create -V tank/test
       zfs set shareiscsi=on tank/test
       zfs share tank/test

The driver will execute the following commands (example!):

  /usr/sbin/ietadm --op new --tid 1 --params Name=iqn.2012-01.com.bayour:share.test1
  /usr/sbin/ietadm --op new --tid 1 --lun 0 --params Path=/dev/zvol/share/test1,Type=fileio

It (the driver) will automatically calculate the TID and IQN and use only the ZVOL
(in this case 'share/test1') in the command lines.


In addition to executing ietadm, it will execute the following script (if it exist
and is executable) '/sbin/zfs_share_iscsi.sh', like so:

  /sbin/zfs_share_iscsi.sh 1

This is so that one can create custom commands to be done on the share.

The only parameter to this script is the TID and the driver will 'execute
and forget'. Meaning, it will not care about exit code nor any output it
gives.


PS. The domainname needs to be set in /proc/sys/kernel/domainname
    for the driver to work out the iqn correctly. You can either
    set it by echo'ing the domain name to the file, OR set it
    using sysctl.
