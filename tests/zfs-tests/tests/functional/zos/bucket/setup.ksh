#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib
DISK=${DISKS%% *}
default_setup $DISK
