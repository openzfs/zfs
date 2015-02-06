zfs diff -J :
```json

{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_diff.json",
    "type":"object",
    "name": "zfs diff -J",
    "version": "1.0" ,
    "description": " describes the changes noted between the ZFS datasets",
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
                        "M": {
                            "type":"string",
                            "description": "Indicates the file directory was modified in the later dataset",
                            "required":false
                        },
                        "R": {
                            "type":"string",
                            "description": "Indicates the file directory was renamed in the later dataset",
                            "required":false
                        },
                        "-": {
                            "type":"string",
                            "description": "Indicates the file directory was removed in the later dataset",
                            "required":false
                        },
                        "+": {
                            "type":"string",
                            "description": "Indicates the file directory was added in the later dataset",
                            "required":false
                        }
                    }
                }
        }
    }
}