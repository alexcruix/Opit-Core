# C++/CLI bridge to LibreHardwareMonitor (separate /clr DLL, loaded at runtime).
# O /clr so existe no MSVC; noutro SO este subdirectorio nem e declarado, e o
# provider de ventoinhas do modelo correspondente e o gemeo sysfs/hwmon.
if(WIN32)
    add_subdirectory(lhm_bridge)
endif()

add_library(opitcore_fans STATIC
    FanTypes.h
    FanProvider.h
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
)

if(WIN32)
    # ACPI/WMI, NVAPI, ADL, iGPU Intel, EC de portátil e a bridge C++/CLI precisam do MSVC e do
    # SCM: fora do Windows nem são declarados (a lista é exactamente a de sempre, só movida).
    target_sources(opitcore_fans PRIVATE
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
        lhm_bridge/OpitLhmBridge.h
        LibreHardwareMonitorFanProvider.h
        LibreHardwareMonitorFanProvider.cpp
    )
else()
    # O kernel publica sensores e duty por /sys/class/hwmon: um provider cobre o que no Windows
    # está dividido entre ACPI, NVAPI, ADL e Intel.
    target_sources(opitcore_fans PRIVATE
        HwmonFanProvider.h
        HwmonFanProvider.cpp
    )
endif()

target_include_directories(opitcore_fans
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/drivers.gpu.control-library-master/include
)

target_link_libraries(opitcore_fans
    PUBLIC
        opitcore_utils
        opitcore_hardware
        opitcore_metrics
        opitcore_security
)