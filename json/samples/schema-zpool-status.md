zpool status -J :

```json


{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zpool_status.json",
    "type":"object",
    "name": "zpool stastus -J",
    "description": " Displays  the detailed health status for the given pools",
    "required":false,
    "properties":{
        "stdout": {
            "type":"array",
            "name": "stdout",
            "description": "standard output of command",
            "minitems": "0",
            "required":false,
            "items":
                {
                    "type":"object",
                    "name": "stdout",
                    "description": "standard output",
                    "required":false,
                    "properties":{
                        "config": {
                            "type":"object",
                            "name": "configation",
                            "description": "configuration of pool",
                            "required":false,
                            "properties":{
                                "checksum": {
                                    "type":"string",
                                    "name": "checksum",
                                    "description": "numbers of checksum errors",
                                    "required":false
                                },
                                "devices": {
                                    "type":"array",
                                    "name": "devices",
                                    "description": "devices presents in zpool",
                                    "minitems": "0",
                                    "required":false,
                                    "items":
                                        {
                                            "type":"object",
                                            "name": "device",
                                            "description": "device in zpool",
                                            "required":false,
                                            "properties":{
                                                "checksum": {
                                                    "type":"string",
                                                    "name": "checksum",
                                                    "description": "Checksum errors, meaning that the device returned corrupted data as the result of a read request",
                                                    "required":false
                                                },
                                                "name": {
                                                    "type":"string",
                                                    "name": "name",
                                                    "description": "name of device",
                                                    "required":false
                                                },
                                                "read": {
                                                    "type":"string",
                                                    "name": "read",
                                                    "description": "I/O errors that occurred while issuing a read request",
                                                    "required":false
                                                },
                                                "state": {
                                                    "type":"string",
                                                    "name": "state",
                                                    "description": "Describes what is wrong with the pool. This field is omitted if no errors are found.",
                                                    "required":false
                                                },
                                                "write": {
                                                    "type":"string",
                                                    "name": "write",
                                                    "description": "I/O errors that occurred while issuing a write request",
                                                    "required":false
                                                }
                                            }
                                        }


                                },
                                "name": {
                                    "type":"string",
                                    "name": "name",
                                    "description": "name of pool",
                                    "required":false
                                },
                                "read": {
                                    "type":"string",
                                    "name": "read",
                                    "description": "I/O errors that occurred while issuing a read request",
                                    "required":false
                                },
                                "state": {
                                    "type":"string",
                                    "name": "state",
                                    "description": "Indicates the current health of the pool. This information refers only to the ability of the pool to provide the necessary replication level.",
                                    "required":false
                                },
                                "write": {
                                    "type":"string",
                                    "name": "write",
                                    "description": "I/O errors that occurred while issuing a write request",
                                    "required":false
                                }
                            }
                        },
                        "pool": {
                            "type":"string",
                            "name": "pool",
                            "description": "Identifies the name of the pool.",
                            "required":false
                        },
                        "scan": {
                            "type":"string",
                            "name": "scan",
                            "description": "Identifies the current status of a scan operation, which might include the date and time that the last scrub was completed, a scrub is in progress, or if no scan was requested.",
                            "required":false
                        },
                        "state": {
                            "type":"string",
                            "name": "state",
                            "description": "Describes what is wrong with the pool. This field is omitted if no errors are found.",
                            "required":false
                        },
                        "stderr": {
                            "type":"string",
                            "name": "stderr",
                            "description": "error output of comand",
                            "required":false
                        }
                    }
                }


        }
    }
}
```
