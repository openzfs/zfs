
zfs mount -J :
 ```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schema/schema_zfs_mount.json",
    "type":"object",
    "name": "zfs mount -J",
    "version": "1.0",
    "description": "mount a filesystem",
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
            "required":true,
             "items":
                {
                    "type":"object",
                    "type": "type of volume",
                    "required":false,
                    "properties":{
                        "volume": {
                            "type":"string",
                            "description": "name of the  volume ",
                            "required":false
                        },
                        "mountpoint": {
                            "type":"string",
                            "description": "mountpoint of: volume in filesystem",
                            "required":false
                        }
        }
    }
}
