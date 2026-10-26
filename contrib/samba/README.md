# timemachine_dfree

Fast, zero-shell ZFS `userquota` / `groupquota` helper for Samba's
`dfree command`, written in C using `libzfs` / `libzfs_core`.

## Problem Statement

When a directory is shared over Samba as an Apple Time Machine disk and has
the setting in smb.conf

```ini
fruit:time machine max size = 4T
```

Samba has to walk the directory tree to count the disk free space left. After a
couple of years and 1000s of backups later it gets a little slow. Using samba's
dfree command one can use this utility to get the total, and available blocks.

This is easily fixed using a shell script:
[Go away or I will replace you with a very small shell script - Easy Mode]

```bash
set -- $(/sbin/zfs get -Hpo value userquota@timemachine,\
  userused@timemachine tank)
echo "$(( $1 / 1024 )) $(( ($1 - $2) / 1024 )) 1024"
```

It has no error handling and can show negative numbers when you run out of
space.

[Hard Mode]
The shell script works great until you run out of space.

## Building & Installing
Install to /usr/local/bin

```bash
make
sudo make install
```

## Usage

### Command Line

```bash
# Query ZFS userquota/userused for user 'timemachine' on dataset 'tank'
/usr/local/bin/timemachine_dfree timemachine tank
```

Output format (`<total_1k_blocks> <avail_1k_blocks> 1024`):

```text
4294967296 3032833050 1024
```

Error handling:
If the user doesn't have a quota it reports:

```bash
/usr/local/bin/timemachine_dfree root  tank
Error: Failed to read valid ZFS userquota/groupquota for
user 'root' on dataset 'tank'.
```

### Samba Configuration (`/etc/samba/smb.conf`)
[Recommended usage through Samba configuration file]

When Samba executes `dfree command`, `smbd` invokes the binary with `"."` as
its sole argument (`timemachine_dfree .`) while running under the connected SMB
user's effective UID (`geteuid()`) inside the share's root directory. In this
mode, `timemachine_dfree` automatically resolves
`<username>` from `geteuid()` and `<pool_name>` from `/proc/mounts`:
In our case it runs as timemachine.

Example Samba config for TimeMachine share.

```ini
[timemachine]

comment = Time Machine Backup
path = /tank/backup
available = yes
valid users = timemachine
browseable = no
guest ok = no
writable = yes
create mask = 0600
directory mask = 0700
case sensitive = yes
default case = lower
preserve case = yes
short preserve case = yes
ea support = yes
store dos attributes = no
vfs objects = catia fruit streams_xattr io_uring
fruit:metadata = stream
fruit:resource = xattr
fruit:veto_appledouble = no
fruit:wipe_intentionally_left_blank_rfork = no
fruit:delete_empty_adfiles = no
fruit:model = TimeCapsule
fruit:time machine = yes
dfree command = /usr/local/bin/timemachine_dfree
dfree cache time = 60
```

```bash
# ZFS quota configuration:
sudo zfs set userquota@timemachine=4T tank
sudo zfs set groupquota@timemachine=4T tank
```

## License

SPDX-License-Identifier: `CDDL-1.0`
