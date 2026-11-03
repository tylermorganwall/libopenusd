cmake_minimum_required(VERSION 3.27)
project(libopenusd LANGUAGES C CXX)

# The R package supplies its compiler flags and TBB from RcppParallel. OpenUSD
# is built once into this DLL, so consumers never depend on its C++ ABI.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_MONOLITHIC ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
foreach(option PYTHON_SUPPORT GL_SUPPORT METAL_SUPPORT MATERIALX_SUPPORT)
  set(PXR_ENABLE_${option} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(option TESTS EXAMPLES TUTORIALS USD_TOOLS IMAGING USD_IMAGING USDVIEW
               DOCUMENTATION PYTHON_DOCUMENTATION EXEC USD_VALIDATION)
  set(PXR_BUILD_${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(PXR_VALIDATE_GENERATED_CODE OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(PXR_FIND_TBB_IN_CONFIG OFF CACHE BOOL "" FORCE)
set(PXR_SET_INTERNAL_NAMESPACE libopenusd_v26_08 CACHE STRING "" FORCE)
set(PXR_OVERRIDE_PLUGINPATH_NAME LIBOPENUSD_PLUGIN_PATH CACHE STRING "" FORCE)
add_subdirectory(OpenUSD)

add_library(libopenusd MODULE bridge.cpp init.cpp inspect.cpp)
target_compile_definitions(libopenusd PRIVATE PXR_STATIC)
set_target_properties(libopenusd PROPERTIES PREFIX "" SUFFIX "${R_SHARED_SUFFIX}")
target_include_directories(libopenusd PRIVATE
  "${RCPP_INCLUDE_DIR}" "${R_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../inst/include"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenUSD/include")
# Whole-archive retains OpenUSD's static schema and file-format registrars.
target_link_libraries(libopenusd PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,usd_m>")
if(APPLE)
  target_link_options(libopenusd PRIVATE "LINKER:-undefined,dynamic_lookup")
elseif(WIN32)
  target_sources(libopenusd PRIVATE exports.def)
  target_link_libraries(libopenusd PRIVATE "${R_LIBRARY}")
endif()
