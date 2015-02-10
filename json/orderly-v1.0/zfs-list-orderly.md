zfs-list -J:

```json

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {
    string available`{"description":"space available in volume ","required":true}`;
    string mountpoint`{"description":"mountpoint of volume in filesystem","required":false}`;
    string name`{"description":"name of volume","required":false}`;
    string referenced`{"description":"The amount of data that is accessible by this dataset","required":false}`;
    string used`{"description":"space used in volume","required":false}`;}*`{"required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  schema/schema_zfs_list.json",
  "name": "zfs list -J",
  "version": "1.0",
  "description": "list all volume of filesystem",
  "required": true
}`;
```