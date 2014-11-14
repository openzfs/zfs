Native ZFS for Linux!

ZFS is an advanced file system and volume manager which was originally
developed for Solaris and is now maintained by the Illumos community.

ZFS on Linux, which is also known as ZoL, is currently feature complete.  It
includes fully functional and stable SPA, DMU, ZVOL, and ZPL layers.

Full documentation for installing ZoL on your favorite Linux distribution can
be found at: <http://zfsonlinux.org>

The goal of this fork, is to jsonify all ZFS commands output (such as zfs list, zpool list). To do so, we reuse some existing functions and have implemented the -J flag, we have created a specific function to display json (print_json) which takes two (char*)arrays as arguments (keywords and values).

This feature request from Alyseo and project goal history is available here :

http://lists.open-zfs.org/pipermail/developer/2014-September/000847.html

Example implemented for "zfs list" :

Standard output :

```
zfs list
NAME         USED  AVAIL  REFER  MOUNTPOINT
tank        7,66M  1,48G    29K  /tank
tank/test   1,25M  1,48G    16K  -
tank/test1  1,25M  1,48G    16K  -
tank/test2  1,25M  1,48G    16K  -
tank/test3  1,25M  1,48G    16K  -
tank/test4  1,25M  1,48G    16K  -
tank/test5  1,25M  1,48G    16K  -
```

the json output :

```json
{"cmd":"zfs list","output:":[{"name":"tank","used":"8035328","available":"1585800192","referenced":"29696","mountpoint":"/tank"},{"name":"tank/test","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"},{"name":"tank/test1","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"},{"name":"tank/test2","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"},{"name":"tank/test3","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"},{"name":"tank/test4","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"},{"name":"tank/test5","used":"1310720","available":"1587094528","referenced":"16384","mountpoint":"-"}]}
```

Output of command can be piped on 'python -mjson.tool' to have pretty json code :

```
{
    "cmd": "zfs list",
    "output:": [
        {
            "available": "1585800192",
            "mountpoint": "/tank",
            "name": "tank",
            "referenced": "29696",
            "used": "8035328"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test",
            "referenced": "16384",
            "used": "1310720"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test1",
            "referenced": "16384",
            "used": "1310720"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test2",
            "referenced": "16384",
            "used": "1310720"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test3",
            "referenced": "16384",
            "used": "1310720"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test4",
            "referenced": "16384",
            "used": "1310720"
        },
        {
            "available": "1587094528",
            "mountpoint": "-",
            "name": "tank/test5",
            "referenced": "16384",
            "used": "1310720"
        }
    ]
}
```
