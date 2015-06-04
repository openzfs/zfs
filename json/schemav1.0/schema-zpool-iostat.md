zpool  iostat -J :
```json

{
    "type":"object",
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_iostat.json" ,
    "name": "zpool iostat -J",
    "version": "1.0", 
    "required":false,
    "properties":{
        "stderr": {
            "type":"string",
            "required":false
        },
        "stdout": {
            "type":"array",
            "required":false,
            "items":
                {
                    "type":"object",
                    "required":false,
                    "properties":{
                        "bandwidth read": {
                            "type":"string",
                            "required":false
                        },
                        "capacity allocated": {
                            "type":"string",
                            "required":false
                        },
                        "capacity free": {
                            "type":"string",
                            "required":false
                        },
                        "operation read": {
                            "type":"string",
                            "required":false
                        },
                        "operation write": {
                            "type":"string",
                            "required":false
                        },
                        "pool": {
                            "type":"string",
                            "required":false
                        }
                    }
                }
            

        }
    }
}
