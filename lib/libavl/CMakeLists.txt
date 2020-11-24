
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

set(AVL_MODULE_DIR "../../module/avl")
add_library(libavl
	"${AVL_MODULE_DIR}/avl.c"
)
target_link_libraries(libavl PRIVATE libspl)
