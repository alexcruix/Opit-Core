add_library(opitcore_platform_windows STATIC
  ProcessManager.h
  ProcessManager.cpp
  ProcessGovernor.h
  ProcessGovernor.cpp
  ElevationHelper.h
  ElevationHelper.cpp
  GpuDetector.h
  GpuDetector.cpp
  ServiceManager.h
  ServiceManager.cpp
)

target_include_directories(opitcore_platform_windows
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_platform_windows
  PUBLIC
    opitcore_utils
)
