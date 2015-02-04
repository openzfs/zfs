zfs list -J :

```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_list.json",
    "type":"object",
    "name": "zfs list -J",
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
                        "available": {
                            "type":"string",
                            "description": "space available in volume ",
                            "required":true
                        },
                        "mountpoint": {
                            "type":"string",
                            "description": "mountpoint of volume in filesystem",
                            "required":false
                        },
                        "name": {
                            "type":"string",
                            "description": "name of volume",
                            "required":false
                        },
                        "referenced": {
                            "type":"string",
                            "description": "The amount of data that is accessible by this dataset",
                            "required":false
                        },
                        "used": {
                            "type":"string",
                            "description": "space used in volume",
                            "required":false
                        }
                    }
                }
        }
    }
}


```
