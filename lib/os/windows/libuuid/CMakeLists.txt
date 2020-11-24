
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libuuid
	clear.c
	compare.c
	copy.c
	gen_uuid.c
	isnull.c
	pack.c
	parse.c
	randutils.c
	unpack.c
	unparse.c
	uuid_time.c
	randutils.h
	uuid.h
)
target_compile_definitions(libuuid PUBLIC HAVE_STDINT_H)
target_link_libraries(libuuid PUBLIC libspl)
target_include_directories(libuuid PUBLIC "")
