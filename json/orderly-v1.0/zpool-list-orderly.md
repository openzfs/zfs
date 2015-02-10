zpool list -J:
```json

object {
  string stderr `{
  "name": "stderr",
  "required": true
}`;
  array [object {string allocated`{"name":"allocated","description":"space allocated for zpool","required":false}`;
  string altroot`{"name":"altroot","description":"show if altroot is set or no","required":false}`;
  string capacity`{"name":"capacity","description":"Percentage of pool space used","required":false}`;
  string dedupratio`{"name":"dedup ratio","description":"ration of dedup","required":false}`;
  string expandsize`{"name":"expandsize","description":"  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.","required":false}`;
  string fragmentation`{"name":"fragmentation","description":"  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.","required":false}`;
  string free`{"name":"free","description":"  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.","required":false}`;
  string health`{"name":"health","description":"The current health of the pool. Health can be ONLINE, DEGRADED, FAULTED, OFF‐LINE, REMOVED, or UNAVAIL","required":false}`;
  string name`{"name":"name","description":"name of the pool","required":false}`;
  string size`{"name":"size","description":"Total size of the storage pool.","required":false}`;
  }*`{"required":false}`]* 
  stdout `{
  "name": "stdout",
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  schema/schema_zpool_list.json",
  "name": "zpool list -J",
  "version": "1.0",
  "description": "list all zpool of file sytstem",
  "required": false
}`;

```