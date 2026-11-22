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
    winhttp
)
