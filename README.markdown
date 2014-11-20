Native ZFS for Linux!

ZFS is an advanced file system and volume manager which was originally
developed for Solaris and is now maintained by the Illumos community.

ZFS on Linux, which is also known as ZoL, is currently feature complete.  It
includes fully functional and stable SPA, DMU, ZVOL, and ZPL layers.

Full documentation for installing ZoL on your favorite Linux distribution can
be found at: <http://zfsonlinux.org>

The goal of this fork, is to jsonify all ZFS commands output (such as zfs list, zpool list). To do so, we reuse some existing functions and have implemented the "-J" and "-j" flags, we used the json library of joyent based on nvpairlib : 
https://github.com/joyent/illumos-joyent/blob/master/usr/src/lib/libnvpair/nvpair_json.c.

The option "-J" is for json output.

The option "-j" is for an ldjson output (http://en.wikipedia.org/wiki/Line_Delimited_JSON).

This feature request from Alyseo and project goal history is available here : 

http://lists.open-zfs.org/pipermail/developer/2014-September/000847.html 	
http://lists.open-zfs.org/pipermail/developer/2014-November/000934.html

Example implemented for "zfs list" :

Standard output :

```
zfs list
NAME        USED  AVAIL  REFER  MOUNTPOINT
tank       1,31M   983M    19K  /tank
tank/toto  1,25M   984M     8K  -
tank1      59,5K   984M    19K  /tank1
tank2      59,5K  3,72G    19K  /tank2
```

Json output with -J:

```json
{"stdout":[{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"},{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"},{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"},{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}],"stderr":""}
```

Output (with -J only) can be piped on 'python -mjson.tool' to have pretty json code :
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
Json output with -j :
```json
{"name":"tank","used":"1376256","available":"1030422528","referenced":"19456","mountpoint":"/tank"}
{"name":"tank/toto","used":"1310720","available":"1031725056","referenced":"8192","mountpoint":"-"}
{"name":"tank1","used":"60928","available":"1031737856","referenced":"19456","mountpoint":"/tank1"}
{"name":"tank2","used":"60928","available":"3996586496","referenced":"19456","mountpoint":"/tank2"}
{"stderr":""}
```
