add_library(opitcore_adaptive STATIC
  TelemetryValidator.h
  TelemetryValidator.cpp
  AppProfileManager.h
  AppProfileManager.cpp
  BottleneckDiagnoser.h
  BottleneckDiagnoser.cpp
  RecoveryJournal.h
  RecoveryJournal.cpp
  AdaptiveEngine.h
  AdaptiveEngine.cpp
)

target_include_directories(opitcore_adaptive
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(opitcore_adaptive
  PUBLIC
    opitcore_utils
    opitcore_metrics
    opitcore_power
    opitcore_platform_windows
)
