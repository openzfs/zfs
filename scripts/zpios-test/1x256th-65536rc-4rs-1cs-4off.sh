#!/bin/bash
#
#
# Usage: zpios
#         --chunksize         -c    =values
#         --chunksize_low     -a    =value
#         --chunksize_high    -b    =value
#         --chunksize_incr    -g    =value
#         --offset            -o    =values
#         --offset_low        -m    =value
#         --offset_high       -q    =value
#         --offset_incr       -r    =value
#         --regioncount       -n    =values
#         --regioncount_low   -i    =value
#         --regioncount_high  -j    =value
#         --regioncount_incr  -k    =value
#         --threadcount       -t    =values
#         --threadcount_low   -l    =value
#         --threadcount_high  -h    =value
#         --threadcount_incr  -e    =value
#         --regionsize        -s    =values
#         --regionsize_low    -A    =value
#         --regionsize_high   -B    =value
#         --regionsize_incr   -C    =value
#         --cleanup           -x
#         --verify            -V
#         --zerocopy          -z
#         --threaddelay       -T    =jiffies
#         --regionnoise       -I    =shift
#         --chunknoise        -N    =bytes
#         --prerun            -P    =pre-command
#         --postrun           -R    =post-command
#         --log               -G    =log directory
#         --pool | --path     -p    =pool name
#         --load              -L    =dmuio
#         --help              -?    =this help
#         --verbose           -v    =increase verbosity

ZPIOS_CMD="${CMDDIR}/zpios/zpios                                 \
	--load=dmuio                                             \
	--path=${ZPOOL_NAME}                                     \
	--threadcount=1,2,4,8,16,32,64,128,256                   \
	--regioncount=65536                                      \
	--regionsize=4M                                          \
	--chunksize=1M                                           \
	--offset=4M                                              \
        --cleanup                                                \
	--verbose                                                \
	--human-readable                                         \
	${ZPIOS_OPTIONS}"

zpios_start() {
	echo ${ZPIOS_CMD}
	${ZPIOS_CMD} || exit 1
}

zpios_stop() {
	echo
}
