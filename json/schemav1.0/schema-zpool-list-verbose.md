zpool list -Jv :

```json

{
	"type":"object",
	"$schema": "https://github.com/Alyseo/zfs/tree/json/json/schemav1.0/schema/schema_zpool_list_verbose.json",
	"name": "zpool list -Jv",
	"version": "1.0",
	"description": "list all pool whit verbose mode",
	"id": "#zpool list -Jv",
	"required":true,
	"properties":{
		"stderr": {
			"type":"string",
			"name": "stderr",
			"description": "error output of command",
			"id": "#stderr",
			"required":true
		},
		"stdout": {
			"type":"array",
			"name": "stdout",
			"description": "#standard output of command",
			"minitems": "0",
			"id": "#stdout",
			"required":true,
			"items":
				{
					"type":"object",
					"name": "zpool",
					"id": "#zpool",
					"required":false,
					"properties":{
						"allocated": {
							"type":"string",
							"name": "allocated",
							"description": "space allocated for zpool",
							"id": "#allocated",
							"required":false
						},
						"altroot": {
							"type":"string",
							"name": "altroot",
							"description": "show if altroot is set or no",
							"id": "#altroot",
							"required":false
						},
						"capacity": {
							"type":"string",
							"name": "capacity",
							"description": "Percentage of pool space used",
							"id": "#capacity",
							"required":false
						},
						"dedupratio": {
							"type":"string",
							"name": "dedup ratio",
							"description": "ratio of dedup",
							"id": "#dedupratio",
							"required":false
						},
						"devices": {
							"type":"array",
							"title": "device",
							"description": "device used by the pool",
							"minitems": "0",
							"id": "#device",
							"required":false,
							"items":
								{
									"type":"object",
									"name": "device",
									"description": "device present in the pool",
									"id": "#device",
									"required":false,
									"properties":{
										"allocated": {
											"type":"string",
											"name": "allocated",
											"description": "space allocated in the device",
											"id": "#allocated",
											"required":false
										},
										"capacity": {
											"type":"string",
											"name": "capacity",
											"description": "capacity of the device",
											"id": "#capacity",
											"required":false
										},
										"expandsize": {
											"type":"string",
											"name": "expansize",
											"description": "  Amount of uninitialized space within the device that can be used to increase the total capacity",
											"id": "#expandsize",
											"required":false
										},
										"fragmentation": {
											"type":"string",
											"name": "fragmentation",
											"description": "The amount of fragmentatio",
											"id": "#fragmentation",
											"required":false
										},
										"free": {
											"type":"string",
											"name": "free",
											"description": "The amount of free space available",
											"id": "#free",
											"required":false
										},
										"name": {
											"type":"string",
											"name": "name",
											"description": "name of device",
											"id": "#name",
											"required":false
										},
										"size": {
											"type":"string",
											"name": "size",
											"description": "total size of device",
											"id": "#size",
											"required":false
										}
									}
								}
							

						},
						"expandsize": {
							"type":"string",
							"name": "expandsize",
							"description": "  Amount of uninitialized space within the pool or device that can be used to increase the total capacity of the pool",
							"id": "#expansize",
							"required":false
						},
						"fragmentation": {
							"type":"string",
							"name": "fragmentation",
							"description": " The amount of fragmentation in the pool.",
							"id": "http://jsonschema.net/stdout/0/fragmentation",
							"required":false
						},
						"free": {
							"type":"string",
							"name": "free",
							"description": " The amount of free space available in the pool.",
							"id": "#free",
							"required":false
						},
						"health": {
							"type":"string",
							"name": "health",
							"description": "The amount of free space available in the pool.",
							"id": "#health",
							"required":false
						},
						"name": {
							"type":"string",
							"name": "name",
							"description": "name of pool",
							"id": "#name",
							"required":false
						},
						"size": {
							"type":"string",
							"name": "size",
							"description": "Total size of the storage pool",
							"id": "#size ",
							"required":false
						}
					}
				}
			

		}
	}
}


```
