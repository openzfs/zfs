
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

set(UNICODDE_MODULE_DIR "../../module/unicode")
add_library(libunicode 
	"${UNICODDE_MODULE_DIR}/u8_textprep.c"
    "${UNICODDE_MODULE_DIR}/uconv.c"
)
target_link_libraries(libunicode PRIVATE libspl)
