
Workflow for each operating system:
- install qemu on the github runner
- download current cloud image of operating system
- start and init that image via cloud-init
- install dependencies and poweroff system
- start system and build openzfs and then poweroff again
- clone build system and start 2 instances of it
- run functional testings and complete in around 3h
- when tests are done, do some logfile preparing
- show detailed results for each system
- in the end, generate the job summary

/TR 14.09.2024
