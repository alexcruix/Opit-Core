add_library(opitcore_power STATIC
  PowerManager.h
  PowerManager.cpp
)

target_include_directories(opitcore_power
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_power
  PUBLIC
    opitcore_utils
)
