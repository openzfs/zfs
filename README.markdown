Native ZFS for Linux!

ZFS is an advanced file system and volume manager which was originally developed for Solaris and is now maintained by the Illumos community.

ZFS on Linux, which is also known as ZoL, is currently feature complete. It includes fully functional and stable SPA, DMU, ZVOL, and ZPL layers.

Full documentation for installing ZoL on your favorite Linux distribution can be found at: [zfs on linux web site](http://zfsonlinux.org)

The goal of this fork, is to jsonify all ZFS commands output (such as zfs list, zpool list). To do so, we reuse some existing functions and have implemented the "-J" and "-j" flags, we used the json library of joyent based on nvpairlib : https://github.com/joyent/illumos-joyent/blob/master/usr/src/lib/libnvpair/nvpair_json.c.

The option "-J" is for json output.

The option "-j" is for an ldjson output ([ldjson wikipedia](http://en.wikipedia.org/wiki/Line_Delimited_JSON)).

This feature request from Alyseo and project goal history is available here :

http://lists.open-zfs.org/pipermail/developer/2014-September/000847.html

http://lists.open-zfs.org/pipermail/developer/2014-November/000934.html

http://lists.open-zfs.org/pipermail/developer/2015-February/001242.html

#### Full status (json schemas description, community validation and code implemented) is availlable here :
[STATUS](https://github.com/Alyseo/zfs/blob/json/json/STATUS.md)

## Installation (for Debian)

First install the dependencies 
```bash
apt-get install libtool build-essential gawk alien fakeroot linux-headers-$(uname -r) zlib1g-dev uuid-dev libblkid-dev libselinux-dev parted lsscsi wget automake
```
Next install the spl : 
```bash
git clone https://github.com/zfsonlinux/spl.git
cd spl
./autogen.sh
./configure
make deb
dpkg -i *.deb
cd ..
```
Now you can clone the zfs (branch with json implementation)"
```bash
git clone -b nvlist https://git.alyseo.com/42/zfs-json.git
cd zfs-json
./autogen.sh
./configure
make deb-utils deb-kmod
dpkg -i *.deb
```

## json outputs samples

### Implementation for "zfs list" :
### Standard output :
### zfs list
```bash
NAME          USED  AVAIL       REFER   MOUNTPOINT
tank        1,31M   983M        19K         /tank
tank/toto   1,25M   984M        8K          -
tank1       59,5K   984M        19K         /tank1
tank2       59,5K   3,72G       19K         /tank2
```
### zfs list -J
```json
{"stdout":[{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"},{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"},{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"},{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}],"stderr":""}
```
### zfs list -J | python -mjson.tool
```json
{
    "stderr": "", 
        "stdout": [
        {
            "available": "1030422528", 
            "mountpoint": "/tank", 
            "name": "tank", 
            "referenced": "19456", 
            "used": "1376256"
        }, 
        {
            "available": "1031725056", 
            "mountpoint": "-", 
            "name": "tank/toto", 
            "referenced": "8192", 
            "used": "1310720"
        }, 
        {
            "available": "1031737856", 
            "mountpoint": "/tank1", 
            "name": "tank1", 
            "referenced": "19456", 
            "used": "60928"
        }, 
        {
            "available": "3996586496", 
            "mountpoint": "/tank2", 
            "name": "tank2", 
            "referenced": "19456", 
            "used": "60928"
        }
    ]
}
```

### zfs list -j
```json
{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"}
{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"}
{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"}
{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}
{"stderr":""}
```

## Implementation for "zpool list" :

### zpool list -J
```json
{"stdout":[{"name":"lincoln","size":"1,94G","allocated":"76K","free":"1,94G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"},{"name":"tank","size":"3,23G","allocated":"204K","free":"3,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}],"stderr":""}
```
### zpool list -J | python -m json.tool
```json
{
    "stderr": "", 
    "stdout": [
        {
            "allocated": "76K", 
            "altroot": "-", 
            "capacity": "0%", 
            "dedupratio": "1.00x", 
            "expandsize": "-", 
            "fragmentation": "0%", 
            "free": "1,94G", 
            "health": "ONLINE", 
            "name": "lincoln", 
            "size": "1,94G"
        }, 
        {
            "allocated": "204K", 
            "altroot": "-", 
            "capacity": "0%", 
            "dedupratio": "1.00x", 
            "expandsize": "-", 
            "fragmentation": "0%", 
            "free": "3,23G", 
            "health": "ONLINE", 
            "name": "tank", 
            "size": "3,23G"
        }
    ]
}
```
### zpool list -j
```json
{"name":"lincoln","size":"1,94G","allocated":"76K","free":"1,94G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}
{"name":"tank","size":"3,23G","allocated":"204K","free":"3,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}
{"stderr":""}
```
### zpool list -Jv
```json
{"stdout":[{"name":"lincoln","size":"1,94G","allocated":"76K","free":"1,94G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"mirror","size":"992M","allocated":"28,5K","free":"992M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop4","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop5","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]},{"name":"mirror","size":"992M","allocated":"47,5K","free":"992M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop6","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop7","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]}]},{"name":"tank","size":"3,23G","allocated":"204K","free":"3,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"mirror","size":"1016M","allocated":"71K","free":"1016M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop0","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop1","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]},{"name":"mirror","size":"2,23G","allocated":"132K","free":"2,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop2","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop3","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]}]}],"stderr":""}
```
### zpool list -Jv | python -m json.tool
```json
{
    "stderr": "", 
    "stdout": [
        {
            "allocated": "76K", 
            "altroot": "-", 
            "capacity": "0%", 
            "dedupratio": "1.00x", 
            "devices": [
                {
                    "allocated": "28,5K", 
                    "capacity": "0%", 
                    "devices": [
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop4", 
                            "size": "-"
                        }, 
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop5", 
                            "size": "-"
                        }
                    ], 
                    "expandsize": "-", 
                    "fragmentation": "0%", 
                    "free": "992M", 
                    "name": "mirror", 
                    "size": "992M"
                }, 
                {
                    "allocated": "47,5K", 
                    "capacity": "0%", 
                    "devices": [
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop6", 
                            "size": "-"
                        }, 
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop7", 
                            "size": "-"
                        }
                    ], 
                    "expandsize": "-", 
                    "fragmentation": "0%", 
                    "free": "992M", 
                    "name": "mirror", 
                    "size": "992M"
                }
            ], 
            "expandsize": "-", 
            "fragmentation": "0%", 
            "free": "1,94G", 
            "health": "ONLINE", 
            "name": "lincoln", 
            "size": "1,94G"
        }, 
        {
            "allocated": "204K", 
            "altroot": "-", 
            "capacity": "0%", 
            "dedupratio": "1.00x", 
            "devices": [
                {
                    "allocated": "71K", 
                    "capacity": "0%", 
                    "devices": [
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop0", 
                            "size": "-"
                        }, 
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop1", 
                            "size": "-"
                        }
                    ], 
                    "expandsize": "-", 
                    "fragmentation": "0%", 
                    "free": "1016M", 
                    "name": "mirror", 
                    "size": "1016M"
                }, 
                {
                    "allocated": "132K", 
                    "capacity": "0%", 
                    "devices": [
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop2", 
                            "size": "-"
                        }, 
                        {
                            "allocated": "-", 
                            "capacity": "-", 
                            "expandsize": "-", 
                            "fragmentation": "-", 
                            "free": "-", 
                            "name": "loop3", 
                            "size": "-"
                        }
                    ], 
                    "expandsize": "-", 
                    "fragmentation": "0%", 
                    "free": "2,23G", 
                    "name": "mirror", 
                    "size": "2,23G"
                }
            ], 
            "expandsize": "-", 
            "fragmentation": "0%", 
            "free": "3,23G", 
            "health": "ONLINE", 
            "name": "tank", 
            "size": "3,23G"
        }
    ]
}
```
### zpool list -jv
```json
{"name":"lincoln","size":"1,94G","allocated":"76K","free":"1,94G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"mirror","size":"992M","allocated":"28,5K","free":"992M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop4","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop5","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]},{"name":"mirror","size":"992M","allocated":"47,5K","free":"992M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop6","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop7","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]}]}
{"name":"tank","size":"3,23G","allocated":"204K","free":"3,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"mirror","size":"1016M","allocated":"71K","free":"1016M","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop0","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop1","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]},{"name":"mirror","size":"2,23G","allocated":"132K","free":"2,23G","expandsize":"-","fragmentation":"0%","capacity":"0%","devices":[{"name":"loop2","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"},{"name":"loop3","size":"-","allocated":"-","free":"-","expandsize":"-","fragmentation":"-","capacity":"-"}]}]}
{"stderr":""}
```
### zpool status -J
```json
{"stdout":[{"pool":"tank","state":"ONLINE","scan":"none requested","config":{"name":"tank","state":"ONLINE","read":"0","write":"0","checksum":"0","devices":[{"name":"mirror-0","state":"ONLINE","read":"0","write":"0","checksum":"0","devices":[{"name":"loop0","state":"ONLINE","read":"0","write":"0","checksum":"0"},{"name":"loop1","state":"ONLINE","read":"0","write":"0","checksum":"0"}]}]},"stderr":""},{"pool":"tank1","state":"DEGRADED","status":"One or more devices could not be used because the label is missing or invalid.Sufficient replicas exist for the pool to continuefunctioning in a degraded state.","action":" Replace the device using 'zpool replace'","see":"http://zfsonlinux.org/msg/ZFS-8000-4J","scan":"none requested","config":{"name":"tank1","state":"DEGRADED","read":"0","write":"0","checksum":"0","devices":[{"name":"mirror-0","state":"DEGRADED","read":"0","write":"0","checksum":"0","devices":[{"name":"loop6","state":"UNAVAIL","read":"0","write":"0","checksum":"0"},{"name":"loop5","state":"ONLINE","read":"0","write":"0","checksum":"0"}]}]},"stderr":"corrupted data"}]}
```

### zpool status -J | python -m json.tool
```json
{
    "stdout": [
        {
            "config": {
                "checksum": "0",
                "devices": [
                    {
                        "checksum": "0",
                        "devices": [
                            {
                                "checksum": "0",
                                "name": "loop0",
                                "read": "0",
                                "state": "ONLINE",
                                "write": "0"
                            },
                            {
                                "checksum": "0",
                                "name": "loop1",
                                "read": "0",
                                "state": "ONLINE",
                                "write": "0"
                            }
                        ],
                        "name": "mirror-0",
                        "read": "0",
                        "state": "ONLINE",
                        "write": "0"
                    }
                ],
                "name": "tank",
                "read": "0",
                "state": "ONLINE",
                "write": "0"
            },
            "pool": "tank",
            "scan": "none requested",
            "state": "ONLINE",
            "stderr": ""
        },
        {
            "action": " Replace the device using 'zpool replace'",
            "config": {
                "checksum": "0",
                "devices": [
                    {
                        "checksum": "0",
                        "devices": [
                            {
                                "checksum": "0",
                                "name": "loop6",
                                "read": "0",
                                "state": "UNAVAIL",
                                "write": "0"
                            },
                            {
                                "checksum": "0",
                                "name": "loop5",
                                "read": "0",
                                "state": "ONLINE",
                                "write": "0"
                            }
                        ],
                        "name": "mirror-0",
                        "read": "0",
                        "state": "DEGRADED",
                        "write": "0"
                    }
                ],
                "name": "tank1",
                "read": "0",
                "state": "DEGRADED",
                "write": "0"
            },
            "pool": "tank1",
            "scan": "none requested",
            "see": "http://zfsonlinux.org/msg/ZFS-8000-4J",
            "state": "DEGRADED",
            "status": "One or more devices could not be used because the label is missing or invalid.Sufficient replicas exist for the pool to continuefunctioning in a degraded state.",
            "stderr": "corrupted data"
        }
    ]
}
```
### zpool status -j

```json
{"pool":"tank","state":"ONLINE","scan":"none requested","config":{"name":"tank","state":"ONLINE","read":"0","write":"0","checksum":"0","devices":[{"name":"mirror-0","state":"ONLINE","read":"0","write":"0","checksum":"0","devices":[{"name":"loop0","state":"ONLINE","read":"0","write":"0","checksum":"0"},{"name":"loop1","state":"ONLINE","read":"0","write":"0","checksum":"0"}]}]},"stderr":""}
  {"pool":"tank1","state":"DEGRADED","status":"One or more devices could not be used because the label is missing or invalid.Sufficient replicas exist for the pool to continuefunctioning in a degraded state.","action":" Replace the device using 'zpool replace'","see":"http://zfsonlinux.org/msg/ZFS-8000-4J","scan":"none requested","config":{"name":"tank1","state":"DEGRADED","read":"0","write":"0","checksum":"0","devices":[{"name":"mirror-0","state":"DEGRADED","read":"0","write":"0","checksum":"0","devices":[{"name":"loop6","state":"UNAVAIL","read":"0","write":"0","checksum":"0"},{"name":"loop5","state":"ONLINE","read":"0","write":"0","checksum":"0"}]}]},"stderr":"corrupted data"}
```