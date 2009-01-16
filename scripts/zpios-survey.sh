#!/bin/bash

prog=survey.sh
. ../.script-config

LOG=/home/`whoami`/zpios-logs/`uname -r`/zpios-`date +%Y%m%d`/
mkdir -p ${LOG}

# Apply all tunings described below to generate some best case
# numbers for what is acheivable with some more elbow grease.
NAME="prefetch+zerocopy+checksum+pending1024+kmem"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	"zfs_prefetch_disable=1 zfs_vdev_max_pending=1024 zio_bulk_flags=0x100" \
        "--zerocopy"                                                 \
	${LOG}/${NAME}/                                              \
        "${CMDDIR}/zfs/zfs set checksum=off lustre" |       \
	tee ${LOG}/${NAME}.txt

# Baseline number for an out of the box config with no manual tuning.
# Ideally, we will want things to be automatically tuned and for this
# number to approach the tweaked out results above.
NAME="baseline"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	""                                                           \
        ""                                                           \
	${LOG}/${NAME}/ |                                            \
	tee ${LOG}/${NAME}.txt

# Disable ZFS's prefetching.  For some reason still not clear to me
# current prefetching policy is quite bad for a random workload.
# Allow the algorithm to detect a random workload and not do anything
# may be the way to address this issue.
NAME="prefetch"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	"zfs_prefetch_disable=1"                                     \
        ""                                                           \
	${LOG}/${NAME}/ |                                            \
	tee ${LOG}/${NAME}.txt

# As expected, simulating a zerocopy IO path improves performance
# by freeing up lots of CPU which is wasted move data between buffers.
NAME="zerocopy"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	""                                                           \
        "--zerocopy"                                                 \
	${LOG}/${NAME}/ |                                            \
	tee ${LOG}/${NAME}.txt

# Disabling checksumming should show some (if small) improvement
# simply due to freeing up a modest amount of CPU.
NAME="checksum"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	""                                                           \
        ""                                                           \
	${LOG}/${NAME}/                                              \
        "${CMDDIR}/zfs/zfs set checksum=off lustre" |       \
	tee ${LOG}/${NAME}.txt

# Increasing the pending IO depth also seems to improve things likely
# at the expense of latency.  This should be exported more because I'm
# seeing a much bigger impact there that I would have expected.  There
# may be some low hanging fruit to be found here.
NAME="pending"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	"zfs_vdev_max_pending=1024"                                  \
        ""                                                           \
	${LOG}/${NAME}/ |                                            \
	tee ${LOG}/${NAME}.txt

# To avoid memory fragmentation issues our slab implementation can be
# based on a virtual address space.  Interestingly, we take a pretty
# substantial performance penalty for this somewhere in the low level
# IO drivers.  If we back the slab with kmem pages we see far better
# read performance numbers at the cost of memory fragmention and general
# system instability due to large allocations.  This may be because of
# an optimization in the low level drivers due to the contigeous kmem
# based memory.  This needs to be explained.  The good news here is that
# with zerocopy interfaces added at the DMU layer we could gaurentee
# kmem based memory for a pool of pages.
#
# 0x100 = KMC_KMEM - Force kmem_* based slab
# 0x200 = KMC_VMEM - Force vmem_* based slab
NAME="kmem"
echo "----------------------- ${NAME} ------------------------------"
./zpios.sh                                                           \
	""                                                           \
	"zio_bulk_flags=0x100"                                       \
        ""                                                           \
	${LOG}/${NAME}/ |                                            \
	tee ${LOG}/${NAME}.txt
