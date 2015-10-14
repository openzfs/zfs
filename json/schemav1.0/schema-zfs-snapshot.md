zfs snapshot -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_snapshot.json",
    "type":"object",
    "name": "zfs snapshot -J ",
    "version": "1.0",
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
