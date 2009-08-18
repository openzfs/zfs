#!/bin/bash
#
# Usage: zpios
#        --threadcount       -t    =values
#        --threadcount_low   -l    =value
#        --threadcount_high  -h    =value
#        --threadcount_incr  -e    =value
#        --regioncount       -n    =values
#        --regioncount_low   -i    =value
#        --regioncount_high  -j    =value
#        --regioncount_incr  -k    =value
#        --offset            -o    =values
#        --offset_low        -m    =value
#        --offset_high       -q    =value
#        --offset_incr       -r    =value
#        --chunksize         -c    =values
#        --chunksize_low     -a    =value
#        --chunksize_high    -b    =value
#        --chunksize_incr    -g    =value
#        --regionsize        -s    =values
#        --regionsize_low    -A    =value
#        --regionsize_high   -B    =value
#        --regionsize_incr   -C    =value
#        --load              -L    =dmuio|ssf|fpp
#        --pool              -p    =pool name
#        --name              -M    =test name
#        --cleanup           -x
#        --prerun            -P    =pre-command
#        --postrun           -R    =post-command
#        --log               -G    =log directory
#        --regionnoise       -I    =shift
#        --chunknoise        -N    =bytes
#        --threaddelay       -T    =jiffies
#        --verify            -V
#        --zerocopy          -z
#        --nowait            -O
#        --human-readable    -H
#        --verbose           -v    =increase verbosity
#        --help              -?    =this help


ZPIOS_CMD="${ZPIOS}                                              \
	--load=dmuio                                             \
	--pool=${ZPOOL_NAME}                                     \
	--name=${ZPOOL_CONFIG}                                   \
	--threadcount=1                                          \
	--regioncount=16                                         \
	--regionsize=4M                                          \
	--chunksize=1M                                           \
	--offset=4M                                              \
	--cleanup                                                \
	--human-readable                                         \
	${ZPIOS_OPTIONS}"

zpios_start() {
	if [ ${VERBOSE} ]; then
		ZPIOS_CMD="${ZPIOS_CMD} --verbose"
		echo ${ZPIOS_CMD}
	fi

	${ZPIOS_CMD} || exit 1
}

zpios_stop() {
	[ ${VERBOSE} ] && echo
}
