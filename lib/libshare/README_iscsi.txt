This is iSCSI support for the IET iSCSI targets.

It will call ietmadm to both add or remove a iSCSI
target from the call to 'zfs share':

       zfs create -V tank/test
       zfs set shareiscsi=on tank/test
       zfs share tank/test

PS. The domainname needs to be set in /proc/sys/kernel/domainname
    for the driver to work out the iqn correctly. You can either
    set it by echo'ing the domain name to the file, OR set it
    using sysctl.
