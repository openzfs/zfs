```json 

object {
  string stderr `{
  "name": "stderr",
  "description": "error output of command",
  "required": false
}`;
  array [any]* stdout `{
  "name": "stdout",
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_replace.json",
  "name": "zpool replace -J",
  "version": "1.0",
  "description": "replace a device in a zfs storage pool ",
  "required": true
}`;

