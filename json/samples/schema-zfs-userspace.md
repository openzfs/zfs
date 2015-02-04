zfs userspace -J :

```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema-1.0/schema_zfs_userspace.json",
    "type":"object",
    "name": "zfs userspace -J",
    "version": "1.0" ,
    "description": "list all volume of filesystem",
    "required":true,
    "properties":{
        "stderr": {
            "type":"string",
            "title": "stderr",
            "description": "error output of command",
            "required":true
        },
        "stdout": {
            "type":"array",
            "description": "standard output of command",
            "minitems": "0",
            "required":true,
            "items":
                {
                    "type":"object",
                    "type": "type of volume",
                    "required":false,
                    "properties":{
                        "Type": {
                            "type":"string",
                            "description": "space available in volume ",
                            "required":true
                        },
                        "name": {
                            "type":"string",
                            "description": "name of user",
                            "required":false
                        },
                        "used": {
                            "type":"string",
                            "description": "size used by the user in the volume",
                            "required":false
                        },
                        "quota": {
                            "type":"string",
                            "description": "quota of space for the user ",
                            "required":false
                        }
                    }
                }
        }
    }
}
