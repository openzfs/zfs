zpool detach -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zpool_detach.json",
    "type":"object",
    "name": "zfs detach -J",
    "description": "detach device in a storage pool",
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

