cmake_minimum_required(VERSION 3.22...3.27)
project(libopenusd_vendor LANGUAGES C CXX)

# tools/configure.R supplies all package configuration. This project builds
# only the vendored library; R builds the package interface and shared library.
add_subdirectory("${OPENUSD_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/OpenUSD")
