cmake_minimum_required(VERSION 3.19)
project(lim_ltc_package)

# Build all externals into ./externals (read by max-pretarget.cmake).
set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/externals")

# For universal (Intel + Apple Silicon) macOS builds, uncomment:
# set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

add_subdirectory("source/lim.ltc~")
