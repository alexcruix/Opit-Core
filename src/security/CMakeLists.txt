add_library(opitcore_security STATIC
    SecurityTypes.h
    ManifestVerifier.h
    ManifestVerifier.cpp
    IntegrityVerifier.h
    IntegrityVerifier.cpp
    ModuleVerifier.h
    ModuleVerifier.cpp
    SecurityPolicy.h
    SecurityPolicy.cpp
    SecurityPerformanceMonitor.h
    SecurityPerformanceMonitor.cpp
    RecoveryCoordinator.h
    RecoveryCoordinator.cpp
    AntiReverseEngineering.h
    AntiReverseEngineering.cpp
    OpitSecurityManager.h
    OpitSecurityManager.cpp
    OpitSecrets.h
)

target_include_directories(opitcore_security
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(opitcore_security
    PUBLIC
        opitcore_utils
        opitcore_core
        crypt32
)
