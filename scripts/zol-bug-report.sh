#!/bin/sh

whoami=`whoami`
if [ "$whoami" != "root" ]; then
    echo "ERROR: You need to be root (or run sudo) to use this script"
    exit 1
fi

dbg_dir=`tempfile -d /tmp/$$`
rm $dbg_dir ; mkdir $dbg_dir

# Get number of processors
nr_processors=`grep ^processor /proc/cpuinfo | wc -l`

# Get amount of memory
set -- `free -m | grep ^Mem:`
memory="$2"MB

# Get distribution name and release/version
distribution=`lsb_release --description | sed 's,.*:	,,'`

# Get full uname output
uname_val=`uname -a`

# Get the full dmesg log
dmesg $dbg_dir/dmesg.txt

# Get SPL version
spl_ver=`grep 'SPL: Loaded' $dbg_dir/dmesg.txt | tail -n2 | tail -n1 | sed -e 's/.* module //'`
if [ -z "$spl_ver" ]; then
    for file in /var/log/dmesg.*; do
	if file $file | grep -q 'gzip compressed data'; then
	    cat_cmd="zcat"
	else
	    cat_cmd="cat"
	fi

	spl_ver=`$cat_cmd $file | grep 'SPL: Loaded' | tail -n2 | tail -n1 | sed -e 's/.* module //'`
    done
fi

# Get ZFS version
zfs_ver=`grep 'ZFS: Loaded' $dbg_dir/dmesg.txt | tail -n2 | tail -n1 | sed -e 's/.* module //' -e 's/, .*//'`
if [ -z "$zfs_ver" ]; then
    for file in /var/log/dmesg.*; do
	if file $file | grep -q 'gzip compressed data'; then
	    cat_cmd="zcat"
	else
	    cat_cmd="cat"
	fi

	zfs_ver=`$cat_cmd $file | grep 'ZFS: Loaded' | tail -n2 | tail -n1 | sed -e 's/.* module //' -e 's/, .*//'`
    done
fi

ecc_mem=`dmidecode | grep ECC`
if [ -z "$ecc_mem" ]; then
    ecc_mem="No"
else
    ecc_mem="Yes"
fi

# Go through all values and create a text file
cat > $dbg_dir/system_info.txt <<EOF
Number of processes:     $nr_processors
Amount of free memory:   $memory
Using ECC memory:        $ecc_mem

Distribution:            $distribution

SPL version:             $spl_ver
ZFS version:             $zfs_ver
EOF

# ------------------------------------------------

# Get SPL/ZFS module parameters
(for param in /sys/module/spl/parameters/* /sys/module/zfs/parameters/*; do
    echo "`basename $param`"="`cat $param`"
done) | sort > $dbg_dir/spl_zfs_module_params.txt 

# Get SPL debug log
dbg_path=`cat /proc/sys/kernel/spl/debug/path`
echo -1 > /proc/sys/kernel/spl/debug/dump
i=1
for file in $dbg_path*; do
    spl $file $dbg_dir/spl_debug.log > /dev/null 2>&1

    rm $file
    i=`expr $i + 1`
done

# Get stacks for all processes
echo t > /proc/sysrq-trigger
for i in /proc/*/stack; do
    file=`echo $i | sed "s,/proc/\(.*\)/stack,\1,"`
    cat $i > $dbg_dir/stack_$file.txt
done

# Get list of all processes
ps -ef > $dbg_dir/ps_list.txt

# Get all spl information
for i in `find /proc/spl -type f`; do
    file=`echo "$i" | sed -e 's,^/,,' -e 's,/,_,g'`
    cat $i > $dbg_dir/$file.txt
done

# Get pool configuration
zdb > $dbg_dir/zdb.txt

# Get kernel config
if [ -f /proc/config.gz ]; then
    zcat /proc/config.gz > $dbg_dir/kernel_config.txt
elif [ -f /boot/config-`uname -r` ]; then
    cat /boot/config-`uname -r` > $dbg_dir/kernel_config.txt
fi


# ------------------------------------------------

echo "Information is located in: $dbg_dir"
