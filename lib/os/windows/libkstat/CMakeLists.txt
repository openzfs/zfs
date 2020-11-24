
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libkstat
	kstat.c
	gmatch.c
)
target_link_libraries(libkstat PUBLIC libspl)
