cmake_minimum_required(VERSION 3.16)

add_subdirectory(utils)
add_subdirectory(hardware)
add_subdirectory(metrics)
add_subdirectory(power)
add_subdirectory(protection)
add_subdirectory(control)
if(WIN32)
  add_subdirectory(platform/windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(platform/linux)
endif()
add_subdirectory(adaptive)
add_subdirectory(security)
add_subdirectory(fans)
add_subdirectory(ai)
add_subdirectory(core)

if(OPITCORE_BUILD_UI)
  add_subdirectory(ui)
endif()

# A partir daqui tudo e especifico do Windows: executavel GUI (.rc + manifesto PE), copia
# do bridge .NET e gerador do manifesto assinado por PowerShell. Noutro SO ficam so as
# bibliotecas declaradas acima; o que falta e o linker de cada teste que diz o nome.
if(NOT WIN32)
  return()
endif()

# Main executable
add_executable(OpitCore WIN32 main.cpp OpitCore.rc)

set_target_properties(OpitCore PROPERTIES OUTPUT_NAME "Opit-Core")

if(MSVC)
  target_compile_options(OpitCore PRIVATE /guard:cf)
  target_link_options(OpitCore PRIVATE /GUARD:CF)
  # temporario: mapa de simbolos para diagnostico de morte silenciosa — remover apos diagnostico
  target_link_options(OpitCore PRIVATE "/MAP:$<TARGET_FILE_DIR:OpitCore>/Opit-Core.map")
  # O RT_MANIFEST do OpitCore.rc e o oficial (DPI PerMonitorV2): sem isso o linker
  # embutiria um segundo manifesto (asInvoker sem DPI) e geraria recurso duplicado.
  target_link_options(OpitCore PRIVATE "/MANIFEST:NO")
endif()

target_link_libraries(OpitCore
  PRIVATE
    opitcore_security
    opitcore_core
    opitcore_fans
    opitcore_hardware
    opitcore_metrics
    opitcore_power
    opitcore_control
    opitcore_protection
    opitcore_utils
    opitcore_platform_windows
    opitcore_adaptive
    opitcore_ai
)

# Copy LibreHardwareMonitor runtime files to the output folder after OpitCore target is built (if present in source dir)
foreach(lhm_file LibreHardwareMonitorLib.dll LibreHardwareMonitorLib.pdb LibreHardwareMonitor.exe)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${lhm_file}")
    add_custom_command(TARGET OpitCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${lhm_file}" "$<TARGET_FILE_DIR:OpitCore>/"
    )
  endif()
endforeach()

# The C++/CLI bridge is loaded at runtime (not linked); build it with the app if LHM DLL is present.
if(TARGET opitcore_lhm_bridge)
  add_dependencies(OpitCore opitcore_lhm_bridge)
endif()

# Managed dependencies of LibreHardwareMonitorLib 0.9.6 (resolved from the exe folder).
set(OPITCORE_LHM_DEPENDENCIES
    HidSharp.dll
    DiskInfoToolkit.dll
    RAMSPDToolkit-NDD.dll
    BlackSharp.Core.dll
    System.Memory.dll
    System.Buffers.dll
    System.Numerics.Vectors.dll
    System.Runtime.CompilerServices.Unsafe.dll
)
foreach(dep IN LISTS OPITCORE_LHM_DEPENDENCIES)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${dep}")
    add_custom_command(TARGET OpitCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${dep}" "$<TARGET_FILE_DIR:OpitCore>/"
    )
  endif()
endforeach()

# Gera manifesto de integridade assinado por chave RSA oficial.
# O passo antigo so existia dentro de if(NODE_EXECUTABLE): numa maquina sem Node ele
# desaparecia em silencio, o build saia sem opit_manifest.json/.sig e o escudo de
# integridade recusava a abertura do programa (ManifestMissing). Agora usa o PowerShell
# que ja vem com o Windows e nao ha condicao para pular: sem chave o build falha alto.
set(OPITCORE_POWERSHELL "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe")
if(NOT EXISTS "${OPITCORE_POWERSHELL}")
  find_program(OPITCORE_POWERSHELL NAMES powershell.exe pwsh)
endif()
if(NOT OPITCORE_POWERSHELL)
  message(FATAL_ERROR "PowerShell nao encontrado - sem ele o manifesto assinado nao e gerado e o Opit-Core nao abre.")
endif()

add_custom_command(TARGET OpitCore POST_BUILD
    COMMAND "${OPITCORE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/GenerateManifest.ps1"
            -TargetDir "$<TARGET_FILE_DIR:OpitCore>"
    COMMENT "Gerando manifesto criptografico de integridade assinado (RSA-SHA256)..."
    VERBATIM
)

if(OPITCORE_BUILD_UI)
  target_link_libraries(OpitCore PRIVATE opitcore_ui)
endif()
