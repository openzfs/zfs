zfs create -J :

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
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zfs_create.json",
  "name": "zfs create -J",
  "version": "1.0",
  "description": "create a zvol",
  "required": true
}`;