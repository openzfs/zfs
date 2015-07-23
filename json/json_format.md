# Json format 

JSON (JavaScript Object Notation) is a lightweight data-interchange format. It is easy for humans to read and write. It is easy for machines to parse and generate
JSON is a text format that is completely language independent but uses conventions that are familiar to programmers of the C-family of languages, including C, C++, C#, Java, JavaScript, Perl, Python, and many others. These properties make JSON an ideal data-interchange language.

JSON is built on two structures:

    - A collection of name/value pairs. In various languages, this is realized as an object, record, struct, dictionary, hash     table, keyed list, or associative array.
    -An ordered list of values. In most languages, this is realized as an array, vector, list, or sequence.
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
 ### more information about json-schema <http://json-schema.org/>
 
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

### Status of json implementation :

| command           | implemented in code                   |orderly schema                                                                                                 |                                               schema json                                                             | community validation  |   example json                                                                                            |   example ldjson                                                                                          |
|:-----------------:|:-------------------------------------:|:-----------------------------------------------------------------------------------------------------:        |:--------------------------------------------------------------------------------------------------------------------: | :-------------------: | :--------------------------------------------------------------------------------------------------------:|:--------------------------------------------------------------------------------------------------------: |
|zfs list           |       yes (except json versioning)    | [zfs list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-list-orderly.md)                | [zfs list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-list.md)                   |                       | [zfs list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zfs_list.json.md)                | [zfs list -j](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zfs_list.ldjson.md)              |
|zfs create         |       yes (except json versioning)    | [zfs create -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-create-orderly.md)            | [zfs create -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-create.md)               |                       | [zfs create -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zfs_create.json.md)            | [zfs create -j](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zfs_create.ldjson.md)          |
|zfs destroy        |       yes (except json versioning)    | [zfs-destroy](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-destroy-orderly.md)             | [zfs destroy -J](https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema-zfs-destroy.md)                   |                       |                                                                                                           |                                                                                                           |
|zfs snapshot       |       yes (except json versioning)    | [zfs snapshot](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-snapshot-orderly.md)           | [zfs snapshot -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-snapshot.md)           |                       |                                                                                                           |                                                                                                           |
|zfs clone          |       yes (except json versioning)    | [zfs clone](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-clone-orderly.md)                 | [zfs clone -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-clone.md)                 |                       |                                                                                                           |                                                                                                           |
|zfs promote        |       yes (except json versioning)    | [zfs promote](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-promote-orderly.md)             | [zfs promote -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-promote.md)             |                       |                                                                                                           |                                                                                                           |
|zfs rollback       |       yes (except json versioning)    | [zfs rollback](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-rollback-orderly.md)         | [zfs rollback -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-rollback.md)           |                       |                                                                                                           |                                                                                                           |
|zfs rename         |       yes (except json versioning)    | [zfs rename](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-rename-orderly.md)               | [zfs rename -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-rename.md)               |                       |                                                                                                           |                                                                                                           |
|zfs set            |       yes (except json versioning)    | [zfs set ](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-set-orderly.md)                    | [zfs set -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-set.md)                     |                       |                                                                                                           |                                                                                                           |
|zfs inherit        |       yes (except json versioning)    | [zfs inherit](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-inherit-orderly.md)             | [zfs inherit -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-inherit.md)             |                       |                                                                                                           |                                                                                                           |
|zfs userspace      |       yes (except json versioning)    | [zfs userspace](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-userspace-orderly.md)         | [zfs userspace -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-userspace.md)         |                       |                                                                                                           |                                                                                                           |
|zfs groupspace     |       yes (except json versioning)    | [zfs groupspace](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-groupspace-orderly.md)       | [zfs groupspace -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-groupspace.md)       |                       |                                                                                                           |                                                                                                           |
|zfs mount          |       yes (except json versioning)    | [zfs mount](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-mount-orderly.md)                 | [zfs mount -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-mount.md)                 |                       |                                                                                                           |                                                                                                           |
|zfs unmount        |       yes (except json versioning)    | [zfs umount](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-umount-orderly.md)               | [zfs umount -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-umount.md)               |                       |                                                                                                           |                                                                                                           |
|zfs share          |       yes (except json versioning)    | [zfs share](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-share-orderly.md)                 | [zfs share -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-share.md)                 |                       |                                                                                                           |                                                                                                           |
|zfs unshare        |       yes (except json versioning)    | [zfs ushare](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-ushare-orderly.md)               | [zfs unshare -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-unshare.md)             |                       |                                                                                                           |                                                                                                           |
|zfs send           |       yes (except json versioning)    | [zfs send](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-send-orderly.md)                   | [zfs send -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-send.md)                   |                       |                                                                                                           |                                                                                                           |
|zfs allow          |       yes (except json versioning)    | [zfs-allow](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-allow-orderly.md)                 | [zfs allow -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-allow.md)                 |                       |                                                                                                           |                                                                                                           |
|zfs unallow        |       yes (except json versioning)    | [zfs unallow](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-unallow-orderly.md)             | [zfs unallow -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-unallow.md)             |                       |                                                                                                           |                                                                                                           |
|zfs hold           |       yes (except json versioning)    | [zfs hold](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-hold-orderly.md)                   | [zfs hold -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-hold.md)                   |                       |                                                                                                           |                                                                                                           |
|zfs release        |       yes (except json versioning)    | [zfs release](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-release-orderly.md)             | [zfs release -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-release.md)             |                       |                                                                                                           |                                                                                                           |
|zfs upgrade        |       yes (except json versioning)    | [zfs upgrade](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-upgrade-orderly.md)             | [zfs upgrade -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-upgrade.md)             |                       |                                                                                                           |                                                                                                           |
|zfs receive        |       yes (except json versioning)    | [zfs-receive](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-receive-orderly.md)             | [zfs receive -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-receive.md)             |                       |                                                                                                           |                                                                                                           |
|zfs get            |       yes (except json versioning)    | [zfs get](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-get-orderly.md)                     | [zfs get -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-get.md)                     |                       |                                                                                                           |                                                                                                           |
|zfs diff           |           no                          | [zfs diff](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zfs-diff-orderly.md)                   | [zpool diff -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zfs-diff.md)                 |                       |                                                                                                           |                                                                                                           |
|zfs bookmark       |           n/a                         |          n/a                                                                                                      |                                               n/a                                                                 |           n/a         |                          n/a                                                                              |                              n/a                                                                          |
|zpool get          |           no                          | [zpool get](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-get-orderly.md)                 | [zpool get -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-get.md)                 |                       |                                                                                                           |                                                                                                           |
|zpool list         |       no  | [zpool list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-list-orderly.md)            | [zpool list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-list.md)               |                       | [zpool list -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zpool_list.json.md)            | [zpool list -j](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zpool_list.ldjson.md)          |
|zpool add          |           no                          | [zpool add](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-add-orderly.md)                 | [zpool add -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-add.md)                 |                       |                                                                                                           |                                                                                                           |
|zpool attach       |           no                          | [zpool attach](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-attach-orderly.md)           | [zpool attach -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-attach.md)           |                       |                                                                                                           |                                                                                                           |
|zpool clear        |           no                          | [zpool clear](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-clear-orderly.md)             | [zpool clear -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-clear.md)             |                       |                                                                                                           |                                                                                                           |
|zpool create       |           no                          | [zpool create](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-create-orderly.md)           | [zpool create -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-create.md)           |                       |                                                                                                           |                                                                                                           |
|zpool destroy      |           no                          | [zpool destroy](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-destroy-orderly.md)         | [zpool destroy -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-destroy.md)         |                       |                                                                                                           |                                                                                                           |
|zpool detach       |           no                          | [zpool detach](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-detach-orderly.md)           | [zpool detach -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-detach.md)           |                       |                                                                                                           |                                                                                                           |
|zpool export       |           no                          | [zpool export](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-export-orderly.md)           | [zpool export -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-export.md)           |                       |                                                                                                           |                                                                                                           |
|zpool history      |           no                          | [zpool history](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-history-orderly.md)         | [zpool history -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-history.md)         |                       |                                                                                                           |                                                                                                           |
|zpool labelclear   |           no                          | [zpool labelclear](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-labelclear-orderly.md)   | [zpool labelclear -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-labelclear.md)   |                       |                                                                                                           |                                                                                                           |
|zpool online       |           no                          | [zpool online](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-online-orderly.md)           | [zpool online -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-online.md)           |                       |                                                                                                           |                                                                                                           |
|zpool offline      |           no                          | [zpool offline](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-offline-orderly.md)         | [zpool offline -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-offline.md)         |                       |                                                                                                           |                                                                                                           |
|zpool reguid       |           no                          | [zpool reguid](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-reguid-orderly.md)           | [zpool reguid -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-reguid.md)           |                       |                                                                                                           |                                                                                                           |
|zpool reopen       |           no                          | [zpool reopen](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-reopen-orderly.md)           | [zpool reopen -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-reopen.md)           |                       |                                                                                                           |                                                                                                           |
|zpool remove       |           no                          | [zpool remove](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-remove-orderly.md)           | [zpool remove -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-remove.md)           |                       |                                                                                                           |                                                                                                           |
|zpool replace      |           no                          | [zpool replace](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-replace-orderly.md)         | [zpool replace -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-replace.md)         |                       |                                                                                                           |                                                                                                           |
|zpool scrub        |           no                          | [zpool scrubs](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-scrub-orderly.md)            | [zpool scrub -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-scrub.md)             |                       |                                                                                                           |                                                                                                           |
|zpool set          |           no                          | [zpool set](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-set-orderly.md)                 | [zpool set -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-set.md)                 |                       |                                                                                                           |                                                                                                           |
|zpool split        |           no                          | [zpool split](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-split-orderly.md)             | [zpool split -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-split.md)             |                       |                                                                                                           |                                                                                                           |
|zpool upgrade      |           no                          | [zpool upgrade](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-upgrade-orderly.md)         | [zpool upgrade -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-upgrade.md)         |                       |                                                                                                           |                                                                                                           |
|zpool import       |           no                          | [zpool import](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-import-orderly.md)           | [zpool import -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-import.md)           |                       |                                                                                                           |                                                                                                           |
|zpool iostat       |           no                          | [zpool iostat](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-iostat-orderly.md)           | [zpool iostat -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-iostat.md)           |                       |                                                                                                           |                                                                                                           |
|zpool status       |           no                          | [zpool status -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/orderly-v1.0/zpool-status-orderly.md)        | [zpool status -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/schemav1.0/schema-zpool-status.md)           |                       |[zpool status -J](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zpool_status.json.md)         | [zpool status -j](https://github.com/Alyseo/zfs/blob/json-0.6.4/json/samples/zpool_status.ldjson.md)      |
