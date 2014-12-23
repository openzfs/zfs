Native ZFS for Linux!

ZFS is an advanced file system and volume manager which was originally developed for Solaris and is now maintained by the Illumos community.

ZFS on Linux, which is also known as ZoL, is currently feature complete. It includes fully functional and stable SPA, DMU, ZVOL, and ZPL layers.

Full documentation for installing ZoL on your favorite Linux distribution can be found at: http://zfsonlinux.org

The goal of this fork, is to jsonify all ZFS commands output (such as zfs list, zpool list). To do so, we reuse some existing functions and have implemented the "-J" and "-j" flags, we used the json library of joyent based on nvpairlib : https://github.com/joyent/illumos-joyent/blob/master/usr/src/lib/libnvpair/nvpair_json.c.

The option "-J" is for json output.

The option "-j" is for an ldjson output (http://en.wikipedia.org/wiki/Line_Delimited_JSON).

This feature request from Alyseo and project goal history is available here :

http://lists.open-zfs.org/pipermail/developer/2014-September/000847.html

http://lists.open-zfs.org/pipermail/developer/2014-November/000934.html


## Installation (for debian)

before install the dependencies
```bash
apt-get install libtool build-essential gawk alien fakeroot linux-headers-$(uname -r) zlib1g-dev uuid-dev libblkid-dev libselinux-dev parted lsscsi wget automake
```
next install the spl :
```bash
git clone https://github.com/zfsonlinux/spl.git
cd spl
./autogen.sh
./configure
make deb
dpkg -i *.deb
cd ..
```
now clone the zfs (branch with json implementation)"
```bash
git clone -b json https://github.com/Alyseo/zfs
cd zfs-json
./autogen
./configure
make deb-utils deb-kmod
dpkg -i *.deb
```
## Example

implemented for "zfs list" :

Standard output :

### zfs list
NAME        USED  AVAIL  REFER  MOUNTPOINT
tank       1,31M   983M    19K  /tank
tank/toto  1,25M   984M     8K  -
tank1      59,5K   984M    19K  /tank1
tank2      59,5K  3,72G    19K  /tank2

### Json output with -J:
```json
{"stdout":[{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"},{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"},{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"},{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}],"stderr":""}
```
### Output (with -J only) can be piped on 'python -mjson.tool' to have pretty json code :
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

### Json output with -j :
```json
{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"}
{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"}
{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"}
{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}
{"stderr":""}
```

### Example implemented for "zpool list" :

### zpool list -J
```json
{"stdout":[{"name":"tanka","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"},{"name":"tankb","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}],"stderr":""}
```
### zpool list -J | python -m json.tool
```json
{
    "stderr": "",
    "stdout": [
        {
            "allocated": "57856",
            "altroot": "-",
            "capacity": "0%",
            "dedupratio": "1.00x",
            "expandsize": "-",
            "fragmentation": "0%",
            "free": "3271499264",
            "health": "ONLINE",
            "name": "tanka",
            "size": "3271557120"
        },
        {
            "allocated": "57856",
            "altroot": "-",
            "capacity": "0%",
            "dedupratio": "1.00x",
            "expandsize": "-",
            "fragmentation": "0%",
            "free": "3271499264",
            "health": "ONLINE",
            "name": "tankb",
            "size": "3271557120"
        }
    ]
}
```
### zpool list -j
```json
{"name":"tanka","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}
{"name":"tankb","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-"}
{"stderr":""}
```
### zpool list -Jv
```json
{"stdout":[{"name":"tanka","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"loop0","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0","capacity":"0"}]},{"name":"tankb","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"loop1","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0","capacity":"0"}]}],"stderr":""}
```
### zpool list -Jv |python -m json.tool
```json
{
    "stderr": "",
    "stdout": [
        {
            "allocated": "57856",
            "altroot": "-",
            "capacity": "0%",
            "dedupratio": "1.00x",
            "devices": [
                {
                    "allocated": "57856",
                    "capacity": "0",
                    "expandsize": "-",
                    "fragmentation": "0",
                    "free": "3271499264",
                    "name": "loop0",
                    "size": "3271557120"
                }
            ],
            "expandsize": "-",
            "fragmentation": "0%",
            "free": "3271499264",
            "health": "ONLINE",
            "name": "tanka",
            "size": "3271557120"
        },
        {
            "allocated": "57856",
            "altroot": "-",
            "capacity": "0%",
            "dedupratio": "1.00x",
            "devices": [
                {
                    "allocated": "57856",
                    "capacity": "0",
                    "expandsize": "-",
                    "fragmentation": "0",
                    "free": "3271499264",
                    "name": "loop1",
                    "size": "3271557120"
                }
            ],
            "expandsize": "-",
            "fragmentation": "0%",
            "free": "3271499264",
            "health": "ONLINE",
            "name": "tankb",
            "size": "3271557120"
        }
    ]
}
```
### zpool list -jv
```json
{"name":"tanka","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"loop0","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0","capacity":"0"}]}
{"name":"tankb","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0%","capacity":"0%","dedupratio":"1.00x","health":"ONLINE","altroot":"-","devices":[{"name":"loop1","size":"3271557120","allocated":"57856","free":"3271499264","expandsize":"-","fragmentation":"0","capacity":"0"}]}
{"stderr":""}
```
example implemented for 'zpool status ' :

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
