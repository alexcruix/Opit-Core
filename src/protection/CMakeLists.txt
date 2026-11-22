add_library(opitcore_protection STATIC
  ThermalProtection.h
  ThermalProtection.cpp
)

target_include_directories(opitcore_protection
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_protection
  PUBLIC
    opitcore_utils
)
