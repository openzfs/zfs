zfs list -J :

```json
{
    "stderr": "", 
    "stdout": [
        {
            "available": "1003390976", 
            "mountpoint": "/tank6", 
            "name": "tank6", 
            "referenced": "19456", 
            "used": "2217048064"
        }, 
        {
            "available": "1004693504", 
            "mountpoint": "-", 
            "name": "tank6/tata", 
            "referenced": "8192", 
            "used": "1310720"
        }, 
        {
            "available": "2111203328", 
            "mountpoint": "-", 
            "name": "tank6/tota", 
            "referenced": "8192", 
            "used": "1107820544"
        }, 
        {
            "available": "2111203328", 
            "mountpoint": "-", 
            "name": "tank6/toto", 
            "referenced": "8192", 
            "used": "1107820544"
        }
    ]
}
```