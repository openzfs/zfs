PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin

# Scrub the second Sunday of every month.
24 0 8-14 * * root [ $(date +\%w) -eq 0 ] && [ -x /usr/lib/zfs-linux/scrub ] && /usr/lib/zfs-linux/scrub
