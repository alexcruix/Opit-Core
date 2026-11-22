# C++/CLI bridge to LibreHardwareMonitor (separate /clr DLL, loaded at runtime).
add_subdirectory(lhm_bridge)

add_library(opitcore_fans STATIC
    FanTypes.h
    FanProvider.h
    WindowsAcpiFanProvider.h
    WindowsAcpiFanProvider.cpp
    NvidiaNvapiFanProvider.h
    NvidiaNvapiFanProvider.cpp
    AmdAdlFanProvider.h
    AmdAdlFanProvider.cpp
    IntelGpuFanProvider.h
    IntelGpuFanProvider.cpp
    LaptopFanProvider.h
    LaptopFanProvider.cpp
    MockFanProvider.h
    MockFanProvider.cpp
    FanDiscovery.h
    FanDiscovery.cpp
    FanTelemetry.h
    FanTelemetry.cpp
    FanCurveController.h
    FanCurveController.cpp
    FanSafetyController.h
    FanSafetyController.cpp
    FanDiagnostics.h
    FanDiagnostics.cpp
    FanControlManager.h
    FanControlManager.cpp
    lhm_bridge/OpitLhmBridge.h
    LibreHardwareMonitorFanProvider.h
    LibreHardwareMonitorFanProvider.cpp
)

target_include_directories(opitcore_fans
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/drivers.gpu.control-library-master/include
        ${CMAKE_SOURCE_DIR}/nvapi-main
        ${CMAKE_SOURCE_DIR}/ADLX-main/SDK/Include
        ${CMAKE_SOURCE_DIR}/ADLX-main/SDK/ADLXHelper/Windows/Cpp
)

target_link_libraries(opitcore_fans
    PUBLIC
        opitcore_utils
        opitcore_hardware
        opitcore_metrics
        opitcore_security
)