zfs rename -J :

```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_rename.json",
    "type":"object",
    "name": "zfs rename -J",
    "description": "rename a zfs filesystem",
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

