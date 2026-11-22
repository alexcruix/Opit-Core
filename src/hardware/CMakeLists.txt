add_library(opitcore_hardware STATIC
  HardwareDetection.h
  HardwareDetection.cpp
  SmbiosProbe.h
  SmbiosProbe.cpp
  GpuControlManager.h
  GpuControlManager.cpp
)

target_include_directories(opitcore_hardware
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/drivers.gpu.control-library-master/include
    ${CMAKE_SOURCE_DIR}/nvapi-main
    ${CMAKE_SOURCE_DIR}/ADLX-main/SDK/Include
    ${CMAKE_SOURCE_DIR}/ADLX-main/SDK/ADLXHelper/Windows/Cpp
)

target_link_libraries(opitcore_hardware
  PUBLIC
    opitcore_utils
    opitcore_platform_windows
)
