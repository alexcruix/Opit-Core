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
)

if(WIN32)
    target_link_libraries(opitcore_security
        PUBLIC
            crypt32
    )
else()
    # dl e o que serve o dlopen do libcrypto (SHA-256 e verificacao RSA do manifesto) e do
    # dlopen dos modulos. Em glibc >= 2.34 estas rotinas mudaram para libc, por isso o teste
    # e obrigatorio: ligar -dl a cego partiu o build em qualquer imagem sem libdl.so.2.
    find_library(OPIT_LIB_DL NAMES dl)
    if(OPIT_LIB_DL)
        target_link_libraries(opitcore_security PUBLIC ${OPIT_LIB_DL})
    endif()
endif()
