
use_clang()

add_library(libkstat
	kstat.c
	gmatch.c
)
target_link_libraries(libkstat PUBLIC libspl)
