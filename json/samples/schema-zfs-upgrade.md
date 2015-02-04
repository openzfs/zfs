
zfs upgrade -J :
 ```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_upgrade.json",
    "type":"object",
    "name": "zfs upgrade -J",
    "version": "1.0",
    "description": "Upgrading ZFS vol",
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