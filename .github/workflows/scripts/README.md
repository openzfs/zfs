
Workflow for each operating system:
- install qemu on the github runner
- download current cloud image of operating system
- start and init that image via cloud-init
- install dependencies and poweroff system
- start system and build openzfs and then poweroff again
- clone build system and start 3 instances of it
- the functional testings complete within times < 3h
