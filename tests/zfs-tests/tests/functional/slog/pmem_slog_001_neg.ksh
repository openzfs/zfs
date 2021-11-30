#!/bin/ksh -p

. $STF_SUITE/tests/functional/slog/slog.kshlib

log_assert "ZIL-PMEM does not allow device removal, addition, replacing, offlining or pool splitting"
only_for_zil_default_kind "zil-pmem"
log_onexit cleanup
log_must setup

log_must stat /dev/pmem0
log must stat /dev/pmem1

typeset emsg="ZIL-PMEM requires exactly one PMEM SLOG vdev"

log_must zpool create -f $TESTPOOL mirror $VDEV $VDEV2 log dax:/dev/pmem0

# removal
log_must eval "zpool remove $TESTPOOL pmem0 2>&1 | tee /dev/stderr | grep '$emsg'"

# offlining
log_must eval "zpool offline $TESTPOOL pmem0 2>&1 | tee /dev/stderr | grep '$emsg'"

# replacing
log_must eval "zpool replace $TESTPOOL pmem0 dax:/dev/pmem1 2>&1 | tee /dev/stderr | grep '$emsg'"

# addition
log_must eval "zpool add $TESTPOOL log dax:/dev/pmem1 2>&1 | tee /dev/stderr | grep '$emsg'"
log_must eval "zpool add $TESTPOOL log nodax:/dev/pmem1 2>& 1| tee /dev/stderr | grep '$emsg'"

# split
log_must eval "zpool split $TESTPOOL $TESTPOOL2 2>&1 | tee /dev/stderr | grep '$emsg'"

log_pass ""
