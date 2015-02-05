zfs get -J :
```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_get.json",
    "type":"object",
    "name": "zfs get -J",
    "version": "1.0" ,
    "description": "list properties of  filesystem",
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
                    "required":false,
                    "properties":{
                        "name": {
                            "type":"string",
                            "description": "name of volume ",
                            "required":false
                        },
                        "property": {
                            "type":"string",
                            "description": "name of properties",
                            "required":false
                        },
                        "value": {
                            "type":"string",
                            "description": "value of propertie",
                            "required":false
                        },
                        "source": {
                            "type":"string",
                            "description": "property source",
                            "required":false
                        }
                    }
                }
        }
    }
}