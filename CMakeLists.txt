cmake_minimum_required(VERSION 3.16)

project(OpitCore VERSION 8.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPITCORE_BUILD_TESTS "Build tests" ON)
option(OPITCORE_BUILD_UI "Build UI" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")

# Platform definitions (Windows)
if(WIN32)
  add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE)
  if(MSVC)
    # Os fontes sao UTF-8 sem BOM. Sem esta flag o MSVC decodifica pela ACP (1252) e cada
    # L"acentuado" vira mojibake na tela (141 literais em 6 arquivos de UI).
    add_compile_options(/utf-8)
    # Mitigacoes defensivas de compilacao e execucao PE (ASLR, DEP, Stack Canary)
    add_compile_options(/GS)
    add_link_options(/DYNAMICBASE /NXCOMPAT)
    # HIGHENTROPYVA e uma opcao de 64 bits; no link de 32 bits ela e rejeitada.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      add_link_options(/HIGHENTROPYVA)
    endif()
  endif()
  if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt")
    include_directories(SYSTEM "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt")
  endif()
  # As pastas de import library do SDK mudam com a arquitetura do alvo: fixar "/x64" fazia
  # qualquer build de 32 bits linkar as libs erradas.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OPITCORE_UCRT_LIB "ucrt/x64")
    set(OPITCORE_UM_LIB "um/x64")
  else()
    set(OPITCORE_UCRT_LIB "ucrt/lib")
    set(OPITCORE_UM_LIB "um/lib")
  endif()
  foreach(opitLibDir ${OPITCORE_UCRT_LIB} ${OPITCORE_UM_LIB})
    if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/${opitLibDir}")
      link_directories("C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/${opitLibDir}")
    endif()
  endforeach()
endif()

add_subdirectory(src)

if(OPITCORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
