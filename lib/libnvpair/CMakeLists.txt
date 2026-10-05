
use_clang()

set(NVPAIR_MODULE_DIR "../../module/nvpair")

add_library(libnvpair
	"${NVPAIR_MODULE_DIR}/fnvpair.c"
    "${NVPAIR_MODULE_DIR}/nvpair.c"
    "${NVPAIR_MODULE_DIR}/nvpair_alloc_fixed.c"
    libnvpair.c
    libnvpair_json.c
    nvpair_alloc_system.c
)
target_link_libraries(libnvpair PUBLIC libspl)
