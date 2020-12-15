
use_clang()

wdk_add_library(unicodekern
	u8_textprep.c
	uconv.c
)

target_link_libraries(unicodekern PRIVATE splkern)
