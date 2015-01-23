zpool offline -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zpool_offline.json",
    "type":"object",
    "name": "zpool offline -J",
    "description": "bring a devide offline",
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

