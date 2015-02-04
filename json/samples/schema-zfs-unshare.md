zfs unshare -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zfs_unshare.json",
    "type":"object",
    "name": "zfs unshare -J",
    "version": "1.0",
    "description": "unshare zfs file system",
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

