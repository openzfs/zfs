#!/bin/sh
# shellcheck disable=SC2154


if [ "$1" = "prereqs" ]; then
        echo mdadm mdrun multipath
        exit 0
fi


#
# Helper functions
#
message()
{
        if plymouth --ping 2>/dev/null; then
                plymouth message --text="$*"
        else
                echo "$*" >&2
        fi
        return 0
}

udev_settle()
{
        # Wait for udev to be ready, see https://launchpad.net/bugs/85640
        if [ -x /sbin/udevadm ]; then
                /sbin/udevadm settle --timeout=30
        elif [ -x /sbin/udevsettle ]; then
                /sbin/udevsettle --timeout=30
        fi
        return 0
}


activate_vg()
{
        # Sanity checks
        if [ ! -x /sbin/lvm ]; then
                [ "$quiet" != "y" ] && message "lvm is not available"
                return 1
        fi

        # Detect and activate available volume groups
        /sbin/lvm vgscan
        /sbin/lvm vgchange -a y --sysinit
        return $?
}

udev_settle
activate_vg

exit 0
