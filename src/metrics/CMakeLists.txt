add_library(opitcore_metrics STATIC
  CpuMonitor.h
  CpuMonitor.cpp
  SystemMonitor.h
  SystemMonitor.cpp
)

target_include_directories(opitcore_metrics
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_metrics
  PUBLIC
    opitcore_utils
)
