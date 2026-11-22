add_library(opitcore_utils STATIC
  Types.h
  StringUtils.h
  StringUtils.cpp
  FileUtils.h
  FileUtils.cpp
)

# Ensure no stray compiler options (like stray "c") are passed to this target
set_target_properties(opitcore_utils PROPERTIES COMPILE_OPTIONS "")

target_include_directories(opitcore_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
