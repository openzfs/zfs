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
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_release.json",
  "name": "zfs release -J",
  "version": "1.0",
  "description": "eelease a hold on a snapshot",
  "required": true
}`;
