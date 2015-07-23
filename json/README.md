# Json format 

JSON (JavaScript Object Notation) is a lightweight data-interchange format. It is easy for humans to read and write. It is easy for machines to parse and generate
JSON is a text format that is completely language independent but uses conventions that are familiar to programmers of the C-family of languages, including C, C++, C#, Java, JavaScript, Perl, Python, and many others. These properties make JSON an ideal data-interchange language.

JSON is built on two structures:

    - A collection of name/value pairs. In various languages, this is realized as an object, record, struct, dictionary, hash     table, keyed list, or associative array.

    - An ordered list of values. In most languages, this is realized as an array, vector, list, or sequence.

These are universal data structures. Virtually all modern programming languages support them in one form or another. It makes sense that a data format that is interchangeable with programming languages also be based on these structures.
### example : 
zfs list  :

standard output :

```bash
NAME         USED  AVAIL  REFER  MOUNTPOINT
tank         323K  1,38G    21K  /tank
tank/toto    19K  1,38G    19K  /tank/toto
tank1         55K  1,24G    19K  /tank1
```

zfs list -J :

```json
{"cmd":"zfs list","stdout":[{"name":"tank","used":"81920","available":"1478934528","referenced":"19456","mountpoint":"/tank"},{"name":"tank/toto","used":"19456","available":"1478934528","referenced":"19456","mountpoint":"/tank/toto"},{"name":"tank1","used":"56320","available":"1332683776","referenced":"19456","mountpoint":"/tank1"}],"stderr":{"error":""}}
```

zfs list -J |python -m json.tool :

```json

{
    "cmd": "zfs list",
    "stderr": {
        "error": ""
    },
    "stdout": [
        {
            "available": "1478934528",
            "mountpoint": "/tank",
            "name": "tank",
            "referenced": "19456",
            "used": "81920"
        },
        {
            "available": "1478934528",
            "mountpoint": "/tank/toto",
            "name": "tank/toto",
            "referenced": "19456",
            "used": "19456"
        },
        {
            "available": "1332683776",
            "mountpoint": "/tank1",
            "name": "tank1",
            "referenced": "19456",
            "used": "56320"
        }
    ]
}
```

 ### more information about json here <http://json.org/>
# json schema :
JSON Schema:

    -describes your existing data format
    -clear, human- and machine-readable documentation
    -complete structural validation, useful for :
        -automated testing
        -validating client-submitted data
### example : 
schema for zfs list :

```json
{
    "$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zfs_list.json",
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

 more information about json-schema <http://json-schema.org/>
 
# orderly json

Orderly is an ergonomic micro-language that can represent a subset of JSONSchema. Orderly is designed to feel familiar to the average programmer and to be extremely easy to learn and remember. This document provides a conversational overview of orderly as well as a normative grammar.
 
### example:
zfs-list -J:

standard output :

```bash
NAME         USED  AVAIL  REFER  MOUNTPOINT
tank         323K  1,38G    21K  /tank
tank/toto    19K  1,38G    19K  /tank/toto
tank1         55K  1,24G    19K  /tank1
```


```json

object {
  string stderr `{
  "title": "stderr",
  "description": "error output of command",
  "required": true
}`;
  array [object {
    string available`{"description":"space available in volume ","required":true}`;
    string mountpoint`{"description":"mountpoint of volume in filesystem","required":false}`;
    string name`{"description":"name of volume","required":false}`;
    string referenced`{"description":"The amount of data that is accessible by this dataset","required":false}`;
    string used`{"description":"space used in volume","required":false}`;}*`{"required":false}`]* stdout `{
  "description": "standard output of command",
  "minitems": "0",
  "required": true
}`;
}* `{
  schema/schema_zfs_list.json",
  "name": "zfs list -J",
  "version": "1.0",
  "description": "list all volume of filesystem",
  "required": true
}`;
```

for more inforation about orderly <http://orderly-json.org/docs>
# ldjson
This is a minimal specification for sending and receiving JSON over a stream protocol, such as TCP.
The Line Delimited JSON framing is so simple that no specification had previously been written for this ‘obvious’ way to do it.

 ### example

zfs list :
standard output :

```bash
NAME         USED  AVAIL  REFER  MOUNTPOINT
tank         323K  1,38G    21K  /tank
tank/toto    19K  1,38G    19K  /tank/toto
tank1         55K  1,24G    19K  /tank1
```
zfs list -j :
ld json output:

```json 
{"name":"tank","used":"81920","available":"1478934528","referenced":"19456","mountpoint":"/tank"}
{"name":"tank/toto","used":"19456","available":"1478934528","referenced":"19456","mountpoint":"/tank/toto"}
{"name":"tank1","used":"56320","available":"1332683776","referenced":"19456","mountpoint":"/tank1"}
```
for more information : <https://en.wikipedia.org/wiki/Line_Delimited_JSON>

#### Full status (community validation and code implemented) is availlable here :

[STATUS](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/STATUS.md)