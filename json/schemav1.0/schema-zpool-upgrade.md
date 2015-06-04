
zpool upgrade -J :
 ```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_upgrade.json",
    "type":"object",
    "name": "zpool upgrade -J",
    "version": "1.0",
    "description": "Upgrading ZFS Storage Pools",
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
