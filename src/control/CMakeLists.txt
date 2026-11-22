add_library(opitcore_control STATIC
  GameDetector.h
  GameDetector.cpp
  ModeCoordinator.h
  ModeCoordinator.cpp
)

target_include_directories(opitcore_control
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_control
  PUBLIC
    opitcore_utils
    opitcore_power
    opitcore_hardware
)
