
use_clang()

add_library(libpthread 
	pthread.c
)
target_include_directories(libpthread INTERFACE "")
