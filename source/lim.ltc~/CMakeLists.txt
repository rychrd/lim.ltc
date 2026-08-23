# max-pretarget.cmake derives the project/output name from this folder name
# ("lim.ltc~" -> CMake project "lim.ltc_tilde" -> output "lim.ltc~"),
# sets up the Max include dirs, and (on Windows) the static-CRT flags.
include(${CMAKE_SOURCE_DIR}/max-sdk-base/script/max-pretarget.cmake)

set(LIBLTC_DIR "${CMAKE_SOURCE_DIR}/third_party/libltc")

include_directories(
    "${MAX_SDK_INCLUDES}"
    "${MAX_SDK_MSP_INCLUDES}"
    "${MAX_SDK_JIT_INCLUDES}"
    "${LIBLTC_DIR}"
)

# The external source(s) in this folder.
file(GLOB EXTERNAL_SRC "*.c")

# Vendored libltc, compiled straight into the external (static link).
set(LIBLTC_SRC
    "${LIBLTC_DIR}/ltc.c"
    "${LIBLTC_DIR}/encoder.c"
    "${LIBLTC_DIR}/decoder.c"
    "${LIBLTC_DIR}/timecode.c"
)

add_library(${PROJECT_NAME} MODULE ${EXTERNAL_SRC} ${LIBLTC_SRC})

include(${CMAKE_SOURCE_DIR}/max-sdk-base/script/max-posttarget.cmake)
