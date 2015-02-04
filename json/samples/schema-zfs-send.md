zfs send -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zfs_send.json",
    "type":"object",
    "name": "zfs send -J",
    "version": "1.0",
    "description": "send zfs data",
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

