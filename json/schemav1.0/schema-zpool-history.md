zpool history -J :
```json
{
   "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_history.json",
  "type": "object",
  "name": "zpool history -J",
   "version": "1.0" ,
   "description": "display history of zpool",
   "required":true,
  "properties": {
    "stderr": {
            "type":"string",
            "title": "stderr",
            "description": "error output of command",
            "required":true
    },
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
        "type": "object",
        "properties": {
          "device": {
            "type": "object",
            "properties": {
              "name": {
                "type": "string"
              },
              "history": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "date": {
                      "type": "string"
                    },
                    "cmd": {
                      "type": "string"
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  },
}

