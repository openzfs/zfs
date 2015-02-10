zpool status -J :

```json

object {
  array [object {object {string checksum`{"name":"checksum","description":"numbers of checksum errors","required":false}`;
  array [object {string checksum`{"name":"checksum","description":"Checksum errors, meaning that the device returned corrupted data as the result of a read request","required":false}`;
  string name`{"name":"name","description":"name of device","required":false}`;
  string read`{"name":"read","description":"I/O errors that occurred while issuing a read request","required":false}`;
  string state`{"name":"state","description":"Describes what is wrong with the pool. This field is omitted if no errors are found.","required":false}`;
  string write`{"name":"write","description":"I/O errors that occurred while issuing a write request","required":false}`;
  }*`{"name":"device","description":"device in zpool","required":false}`]
  *devices`{"name":"devices","description":"devices presents in zpool","minitems":"0","required":false}`;
  string name`{"name":"name","description":"name of pool","required":false}`;
  string read`{"name":"read","description":"I/O errors that occurred while issuing a read request","required":false}`;
  string state`{"name":"state","description":"Indicates the current health of the pool. This information refers only to the ability of the pool to provide the necessary replication level.","required":false}`;
  string write`{"name":"write","description":"I/O errors that occurred while issuing a write request","required":false}`;}
  *config`{"name":"configation","description":"configuration of pool","required":false}`;
  string pool`{"name":"pool","description":"Identifies the name of the pool.","required":false}`;
  string scan`{"name":"scan","description":"Identifies the current status of a scan operation, which might include the date and time that the last scrub was completed, a scrub is in progress, or if no scan was requested.","required":false}`;
  string state`{"name":"state","description":"Describes what is wrong with the pool. This field is omitted if no errors are found.","required":false}`;
  string stderr`{"name":"stderr","description":"error output of comand","required":false}`;}
  *`{"name":"stdout","description":"standard output","required":false}`]* stdout `{
  "name": "stdout",
  "description": "standard output of command",
  "minitems": "0",
  "required": false
}`;
}* `{
  "name": "zpool status -J",
  "version": "1.0",
  "description": " Displays  the detailed health status for the given pools",
  "required": false
}`;

```