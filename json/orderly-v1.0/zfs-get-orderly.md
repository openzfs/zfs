```json

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {stringname`{"description":"name of volume ","required":false}`;stringproperty`{"description":"name of properties","required":false}`;stringvalue`{"description":"value of propertie","required":false}`;stringsource`{"description":"property source","required":false}`;}*`{"required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_get.json",
  "name": "zfs get -J",
  "version": "1.0",
  "description": "list properties of filesystem",
  "required": true
}`;
