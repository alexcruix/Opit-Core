add_library(opitcore_ai STATIC
  AiAssistantManager.h
  AiAssistantManager.cpp
)

target_include_directories(opitcore_ai
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(opitcore_ai
  PUBLIC
    opitcore_core
    opitcore_metrics
    opitcore_utils
)

if(WIN32)
  target_link_libraries(opitcore_ai PUBLIC winhttp)
else()
  # Em Linux o transporte (libcurl) e o AES-GCM (libcrypto) entram por dlopen, nao por link:
  # nada e instalado e, sem a biblioteca, o codigo falha fechado. So precisa do -ldl, e este
  # teste e obrigatorio porque em glibc >= 2.34 o dlopen ja vive na libc e ligar -dl a cego
  # partia a ligacao em qualquer imagem sem libdl.so.2.
  find_library(OPIT_LIB_DL NAMES dl)
  if(OPIT_LIB_DL)
    target_link_libraries(opitcore_ai PUBLIC ${OPIT_LIB_DL})
  endif()
endif()
