
Workflow for each operating system:
 - install QEMU on the github runner
 - download current cloud image
 - start and init that image via cloud-init
 - install deps and poweroff system
 - start system and build openzfs and then poweroff again
 - clone the system and start 4 qemu workers for the testings (4x 3GB RAM)
 - use trimable virtual disks (3x 1GB) for each testing system
 - do the functional testings < 3h for each os
