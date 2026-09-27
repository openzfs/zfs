
use_clang()

add_library(libregex
	regcomp.c 
	regerror.c 
	regexec.c 
	regfree.c 
	regsub.c
)

target_compile_definitions(libregex PUBLIC POSIX_MISTAKE)

target_link_libraries(libregex PUBLIC libspl)

