zfs snapshot -j :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_snapshot.json",
    "type":"object",
    "name": "zfs snapshot -j ",
    "description": "create a snapshop ",
    "required":true,
    "properties":{
        "stderr": {
            "type":"string",
            "name": "stderr",
            "description": "error output of command",
            "required":false
        },
        "stdout": {
            "type":"array",
            "name": "stdout",
            "description": "standard output of command",
            "minitems": "0",
            "required":true
        }
    }
}


```
