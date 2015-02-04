zpool reguid -J :

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zpool_reguid.json",
    "type":"object",
    "name": "zpool reguid -J",
    "version": "1.0",
    "description": "Generates a new unique identifier for the pool",
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