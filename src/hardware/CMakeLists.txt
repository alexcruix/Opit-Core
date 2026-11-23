add_library(opitcore_hardware STATIC
  HardwareDetection.h
  HardwareDetection.cpp
  SmbiosProbe.h
  SmbiosProbe.cpp
  GpuControlManager.h
  GpuControlManager.cpp
)

# O gemelo de controle de GPU em Linux fala NVML + sysfs; no Windows o ficheiro e inerte
# (guarda #ifndef _WIN32), por isso entra so na lista de fontes nao-Windows.
if(NOT WIN32)
  target_sources(opitcore_hardware PRIVATE GpuControlManagerLinux.cpp)
  target_link_libraries(opitcore_hardware PUBLIC ${CMAKE_DL_LIBS})
endif()

target_include_directories(opitcore_hardware
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/drivers.gpu.control-library-master/include
)

target_link_libraries(opitcore_hardware
  PUBLIC
    opitcore_utils
    ${OPITCORE_PLATFORM_LIB}
)
