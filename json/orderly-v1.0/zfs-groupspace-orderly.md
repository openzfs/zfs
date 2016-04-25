``` json

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {stringType`{"description":"space available in volume ","required":true}`;stringname`{"description":"name of user","required":false}`;stringused`{"description":"size used by the user in the volume","required":false}`;stringquota`{"description":"quota of space for the user ","required":false}`;}*`{"description":"type of volume","required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_groupspace.json",
  "name": "zfs groupspace -J",
  "version": "1.0",
  "description": "list all volume of filesystem",
  "required": true
}`;
