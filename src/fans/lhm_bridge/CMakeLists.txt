# opitcore_lhm_bridge: the only C++/CLI (/clr) target of Opit-Core.
# It wraps LibreHardwareMonitorLib (.NET Framework 4.7.2) behind a C ABI
# (OpitLhmBridge.h). The native core loads this DLL at runtime, so no native
# target links against it or is compiled with /clr.

set(OPITCORE_LHM_LIB "${CMAKE_SOURCE_DIR}/LibreHardwareMonitorLib.dll")

if(NOT EXISTS "${OPITCORE_LHM_LIB}")
    message(STATUS "LibreHardwareMonitorLib.dll nao encontrada na raiz - pulando compilacao de opitcore_lhm_bridge (.NET/CLI).")
    return()
endif()

add_library(opitcore_lhm_bridge SHARED
    OpitLhmBridge.h
    OpitLhmBridge.cpp
)

target_compile_definitions(opitcore_lhm_bridge PRIVATE OPIT_LHM_BRIDGE_BUILD)

set_target_properties(opitcore_lhm_bridge PROPERTIES
    # Generates /clr and <CLRSupport>true</CLRSupport> for this target only.
    COMMON_LANGUAGE_RUNTIME ""
    # LibreHardwareMonitorLib targets .NET Framework 4.7.2; v4.8 reference
    # assemblies are installed and 4.8 runs 4.7.2 assemblies.
    DOTNET_TARGET_FRAMEWORK_VERSION "v4.8"
    VS_DOTNET_REFERENCES "System;System.Core"
    VS_DOTNET_REFERENCE_LibreHardwareMonitorLib "${OPITCORE_LHM_LIB}"
    # The DLL itself is copied together with its dependencies (src/CMakeLists.txt).
    VS_DOTNET_REFERENCES_COPY_LOCAL OFF
)
