object {
  string stderr `{
  "name": "stderr",
  "description": "error output of command",
  "required": false
}`;
  array [object {stringvolume`{"description":"name of the volume ","required":false}`;stringmountpoint`{"description":"mountpoint of: volume in filesystem","required":false}`;}*`{"description":"type of volume","required":false}`]* stdout `{
  "name": "stdout",
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_mount.json",
  "name": "zfs mount -J",
  "version": "1.0",
  "description": "mount a filesystem",
  "required": true
}`;
