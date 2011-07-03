#!/bin/sh

if [ "${root%%:*}" = "zfs" ]; then
    [ -d /dev/.udev/rules.d ] || mkdir -p /dev/.udev/rules.d
    {
    printf 'KERNEL=="%s", SYMLINK+="root"\n' null 
    printf 'SYMLINK=="%s", SYMLINK+="root"\n' null 
    } >> /dev/.udev/rules.d/99-zfs.rules
    
    # Not sure this is the right way to add these scripts?  Calling initqueue seems 
    # like the 'right' way.  In any case, a race condition means these dir's aren't 
    # always present in time.
    mkdir -p /initqueue-settled /initqueue-finished
    
    printf '[ -e "%s" ] && { ln -s "%s" /dev/root 2>/dev/null; rm "$job"; }\n' \
	"/dev/null" "/dev/null" >> /initqueue-settled/zfssymlink.sh

    echo '[ -e /dev/root ]' > /initqueue-finished/zfs.sh
fi
