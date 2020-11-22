
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

wdk_add_library(nvpairkern
	fnvpair.c
	nvpair.c
	nvpair_alloc_fixed.c
	nvpair_alloc_spl.c
)


target_link_libraries(nvpairkern PRIVATE splkern)
