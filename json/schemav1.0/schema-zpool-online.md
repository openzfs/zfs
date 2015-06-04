zpool online -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_online.json",
    "type":"object",
    "name": "zpool online -J",
    "version": "1.0",
    "description": "bring a devide online",
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

