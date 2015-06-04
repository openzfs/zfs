```json 

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {object {stringname;array [object {stringdate;stringcmd;}*]*history;}*device;}*]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_history.json",
  "name": "zpool history -J",
  "version": "1.0",
  "description": "display history of zpool",
  "required": true
}`;
