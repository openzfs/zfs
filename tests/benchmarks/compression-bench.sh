#!/bin/bash
#Automated ZFS compressiontest
now=$(date +%s)

MODE="NONE"
GZIP="gzip gzip-1 gzip-2 gzip-3 gzip-4 gzip-5 gzip-6 gzip-7 gzip-8 gzip-9"

TYPE="WIKIPEDIA"
TESTRESULTS="test_results_$now.txt"

if [ $# -eq 0 ]
then
        echo "Missing options!"
        echo "(run $0 -h for help)"
        echo ""
        exit 0
fi

while getopts "p:t:ribfhc:" OPTION; do
        case $OPTION in
		p)	
			TESTRESULTS="$OPTARG-$TESTRESULTS"
			echo "Results file of the test is called: ./$TESTRESULTS"
			
			;;
		t)	
			TYPE="$OPTARG"
			case $TYPE in
				[wW])	
					echo "Selected highly compressible Wikipedia file"
					TYPE="WIKIPEDIA"
					;;
				[mM])
					echo "Selected nearly uncompressible MPEG4 file"
					TYPE="MPEG4"
					;;
				[cC])
					echo "Selected custom file named CUSTOM.TEST"
					TYPE="CUSTOM"
					;;
				*)	
					echo "Unknown Selection of Testtype. Using default"
				       	TYPE="WIKIPEDIA"
					;;	
			esac
			;;
                b)
                        MODE="BASIC"
                        ALGO="off lz4 zle lzjb gzip"
                        echo "Selected BASIC compression test"
                        ;;
                f)
                        MODE="FULL"
                        ALGO="off lz4 zle lzjb $GZIP"
                        echo "Selected FULL compression test"
                        echo "This might take a while..."
                        ;;
                c)
                        MODE="CUSTOM"
                        ALGO="$OPTARG"
                        echo "Selected custom compression test using the following algorithms:"
                        echo "$ALGO"
                        ;;
                h)
                        echo "Usage:"
                        echo "$0 -h "
                        echo "$0 -b "
                        echo "$0 -f "
						echo "$0 -c "
                        echo ""
                        echo "   -b    to execute a basic compression test containing: off lz4 zle lzjb gzip zstd"
                        echo "   -f    to execute a full compression test containing all currently available ZFS compression algorithms"
						echo "   -c    to execute the entered list of following compression types: "
						echo "         off lz4 zle lzjb $GZIP"
                        echo ""
						echo "   -p to enter a prefix to the test_result files"
						echo "   -t to select the type of test:"
						echo "      w for highly compressible wikipedia file"
						echo "      m for nearly uncompressible mpeg4 file"
						echo "      c for custom file onder script root named CUSTOM.TEST"
                        echo "   -h     help (this output)"
                        echo "ALL these values are mutually exclusive"
                        exit 0
                        ;;

        esac
done

if [  $MODE = "FULL" -o $MODE = "BASIC" -o $MODE = "CUSTOM" ]
then
        echo "destroy testpool and unmount ramdisk of previous broken/canceled tests"
        test -f ../../cmd/zpool/zpool && sudo ./zfs/cmd/zpool/zpool destroy testpool 2>&1 >/dev/null
        sudo umount -l /mnt/ramdisk >/dev/null 2>&1

        echo "creating ramdisk"
        sudo mkdir /mnt/ramdisk
        sudo mount -t tmpfs -o size=2400m tmpfs /mnt/ramdisk

        echo "creating virtial pool drive"
        truncate -s 1200m /mnt/ramdisk/pooldisk.img

        echo "creating zfs testpool/fs1"
        sudo ../../cmd/zpool/zpool create testpool -f -o ashift=12  /mnt/ramdisk/pooldisk.img
        sudo ../../cmd/zfs/zfs create testpool/fs1
        sudo ../../cmd/zfs/zfs set recordsize=1M  testpool/fs1 

	# Downloading and may be uncompressing file 
	FILENAME=""
	case "$TYPE" in 

		WIKIPEDIA)	
        	echo "downloading and extracting enwik9 testset"
        	sudo wget -nc http://mattmahoney.net/dc/enwik9.zip
        	sudo unzip -n enwik9.zip
			FILENAME="enwik9"
			;;
		MPEG4)
			echo "downloading a MPEG4 testfile"
			sudo wget -nc http://distribution.bbb3d.renderfarming.net/video/mp4/bbb_sunflower_native_60fps_stereo_abl.mp4
			FILENAME="bbb_sunflower_native_60fps_stereo_abl.mp4"
			;;
		CUSTOM)
			if [ -f "CUSTOM.TEST" ]
			then
				echo "CUSTOM.TEST found."
			else
				echo "CUSTOM.TEST not found."
				exit 1
			fi
			FILENAME="CUSTOM.TEST"
			;;
		*)	
			echo "ERROR: $TYPE is not unknown"
			exit 1
			;;
	esac

    echo "copying $FILENAME to ramdisk, truncating it after 1000M"
	sudo dd if=$FILENAME of=/mnt/ramdisk/$FILENAME bs=1M count=1000 status=none
    cd /mnt/ramdisk/
    chksum=`sha256sum $FILENAME`
    cd -
    echo "" >> "./$TESTRESULTS"
    echo "Test with $FILENAME file" >> "./$TESTRESULTS"
	grep "^model name" /proc/cpuinfo |sort -u >> "./$TESTRESULTS"
	grep "^flags" /proc/cpuinfo |sort -u >>  "./$TESTRESULTS"
	echo "" >> "./$TESTRESULTS"
	
    echo "starting compression test suite"
        for comp in $ALGO
        do
            echo "running compression test for $comp"
            ../../cmd/zfs/zfs set compression=$comp testpool/fs1
            echo “Compression results for $comp” >> "./$TESTRESULTS"
            dd if=/mnt/ramdisk/$FILENAME of=/testpool/fs1/$FILENAME bs=4M  2>> "./$TESTRESULTS"
            ../../cmd/zfs/zfs get compressratio testpool/fs1 >> "./$TESTRESULTS"
            echo "" >> "./$TESTRESULTS"
            echo “Decompression results for $comp” >> "./$TESTRESULTS"
            dd if=/testpool/fs1/$FILENAME of=/dev/null bs=4M  2>> "./$TESTRESULTS"
            echo ""  >> "./$TESTRESULTS"
            echo "verifying testhash"
            cd /testpool/fs1/
            chkresult=`echo "$chksum" | sha256sum --check`
            sudo rm $FILENAME
            cd -
            echo "hashcheck result: $chkresult" >> "./$TESTRESULTS"
            echo "" >> "./$TESTRESULTS"
            echo "----" >> "./$TESTRESULTS"
            echo "" >> "./$TESTRESULTS"
        done

        echo "compression test finished"
        echo "destroying pool and unmounting ramdisk"
        sudo ../../cmd/zpool/zpool destroy testpool
        sudo umount -l /mnt/ramdisk

        echo "Done. results writen to test_results_$now.txt "
fi
