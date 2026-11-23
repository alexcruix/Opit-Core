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

# O gemelo de energia em Linux fala cpufreq + sysfs; no Windows o ficheiro e inerte
# (guarda #ifndef _WIN32), por isso entra so na lista de fontes nao-Windows. Depende do
# ElevationHelper, que mora em platform/linux e so se liga la.
if(NOT WIN32)
  target_sources(opitcore_power PRIVATE PowerManagerLinux.cpp)
  target_link_libraries(opitcore_power PUBLIC ${OPITCORE_PLATFORM_LIB})
endif()
