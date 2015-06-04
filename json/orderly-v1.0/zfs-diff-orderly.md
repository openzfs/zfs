```json
object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {stringM`{"description":"Indicates the file directory was modified in the later dataset","required":false}`;stringR`{"description":"Indicates the file directory was renamed in the later dataset","required":false}`;string-`{"description":"Indicates the file directory was removed in the later dataset","required":false}`;string"+"`{"description":"Indicates the file directory was added in the later dataset","required":false}`;}*`{"description":"type of volume","required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_diff.json",
  "name": "zfs diff -J",
  "version": "1.0",
  "description": " describes the changes noted between the ZFS datasets",
  "required": true
}`;
