zpool scrub -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zpool_scrub.json",
    "type":"object",
    "name": "zpool scrub -J",
    "version": "1.0",
    "description": "check file system integrity",
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

