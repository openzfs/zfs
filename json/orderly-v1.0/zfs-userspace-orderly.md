
```json

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {stringtype`{"description":"type of volume","required":false}`;stringname`{"description":"name of volume ","required":true}`;stringused`{"description":"size used by user in volume","required":false}`;stringquota`{"description":"quota of space for the user ","required":false}`;}*`{"required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_userspace.json",
  "name": "zfs userspace -J",
  "version": "1.0",
  "description": "list all volume of filesystem",
  "required": true
}`;
