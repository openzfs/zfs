if [ "${root%%:*}" = "zfs" ]; then
    [ -d /dev/.udev/rules.d ] || mkdir -p /dev/.udev/rules.d
    {
    printf 'KERNEL=="%s", SYMLINK+="root"\n' null 
    printf 'SYMLINK=="%s", SYMLINK+="root"\n' null 
    } >> /dev/.udev/rules.d/99-zfs.rules
    
    printf '[ -e "%s" ] && { ln -s "%s" /dev/root 2>/dev/null; rm "$job"; }\n' \
	"/dev/null" "/dev/null" >> $hookdir/initqueue/settled/zfssymlink.sh

    echo '[ -e /dev/root ]' > $hookdir/initqueue/finished/zfs.sh
fi
