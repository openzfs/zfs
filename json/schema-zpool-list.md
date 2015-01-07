zpool list -J :

```json


{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zpool_list.json",
    "type":"object",
    "name": "zpool list -J",
    "description": "list all zpool of file sytstem",
    "required":false,
    "properties":{
        "stderr": {
            "type":"string",
            "name": "stderr",
            "required":true
        },
        "stdout": {
            "type":"array",
            "name": "stdout",
            "description": "standard output of command",
            "minitems": "0",
            "required":true,
            "items":
                {
                    "type":"object",
                    "required":false,
                    "properties":{
                        "allocated": {
                            "type":"string",
                            "name": "allocated",
                            "description": "sapace allocated for zpool",
                            "required":false
                        },
                        "altroot": {
                            "type":"string",
                            "name": "altroot",
                            "description": "show if altroot is set or no",
                            "required":false
                        },
                        "capacity": {
                            "type":"string",
                            "name": "capacity",
                            "description": "Percentage of pool space used",
                            "required":false
                        },
                        "dedupratio": {
                            "type":"string",
                            "name": "dedup ratio",
                            "description": "ration of dedup",
                            "required":false
                        },
                        "expandsize": {
                            "type":"string",
                            "name": "expandsize",
                            "description": "  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.",
                            "required":false
                        },
                        "fragmentation": {
                            "type":"string",
                            "name": "fragmentation",
                            "description": "  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.",
                            "required":false
                        },
                        "free": {
                            "type":"string",
                            "name": "free",
                            "description": "  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool.",
                            "required":false
                        },
                        "health": {
                            "type":"string",
                            "name": "health",
                            "description": "   The current health of the pool. Health can be "ONLINE", "DEGRADED",  "FAULTED",  "  OFF‐LINE", "REMOVED", or "UNAVAIL"",
                            "required":false
                        },
                        "name": {
                            "type":"string",
                            "name": "name",
                            "description": "name of the pool",
                            "required":false
                        },
                        "size": {
                            "type":"string",
                            "name": "size",
                            "description": "Total size of the storage pool.",
                            "required":false
                        }
                    }
                }


        }
    }
}



```
