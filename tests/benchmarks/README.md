## Current available Benchmarks:

| Benchmark  | File | Description |
| ------------- | ------------- | ------------- |
| **Compression Benchmark**  | compression-bench.sh  | _Generates repeatable compression results_ |

	
	
	
---
	
	
	
## Compression Benchmark
Crude Standardised ZFS compression test, with repeatable results in mind

### Important notes
- Make sure you do not have zfs installed already via other means such as OS packages etc.
- This assumes a clean install with unzip, wget and git installed
- The enwik9 and mpeg4 datasets get downloaded just once, don't worry about the time it takes
- This script makes sure the build/test environment is as prestine as possible before and after running
- This script uses a ramdisk as source and (pool) destination for writes tests to asure clean and unbottlenecked results

### How Use

1. Build ZFS on Linux and install all dependancies as described here: https://github.com/zfsonlinux/zfs/wiki/Building-ZFS#installing-dependencies
2. Make sure you cd'ed into the `benchmarks` directory
3. run: sudo ./compression-bench.sh with one of the **options**

### options

**Compression Tests**
- **-b** A basic test of only the following algorithms: off lz4 zle lzjb gzip zstd
- **-f** A full test of all compression algorithms available for ZFS
- **-c** A Custom test with only the argument algorithm
- **-t** Select a different compression test file. Options: `enwik9` and `mpeg4`
- **-p** Enter a different prefix for the test results

**Other**
- **-h** Displays a help page, which includes a reference to the different commands

When finished you'll have a .txt file in the benchmarks directory, containing the test results
