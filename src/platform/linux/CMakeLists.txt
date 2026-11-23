add_library(opitcore_platform_linux STATIC
  ElevationHelper.h
  ElevationHelper.cpp
  GpuDetector.h
  GpuDetector.cpp
  LinuxProcUtil.h
  # O gemelo de ProcessManager nao traz cabecalho proprio: a declaracao e a mesma do
  # platform/windows/ProcessManager.h, incluida pelo caminho a partir de src, para que o
  # programa tenha uma unica definicao da classe em qualquer sistema operativo.
  ProcessManager.cpp
)

target_include_directories(opitcore_platform_linux
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_platform_linux
  PUBLIC
    opitcore_utils
)

# O NVML nao e ligado em tempo de build: tal como no Windows, carrega-se por dlopen em
# tempo de execucao, para que a ausencia do driver deixe o programa a correr com a GPU
# declarada como indisponivel em vez de recusar o executavel inteiro.
target_link_libraries(opitcore_platform_linux PUBLIC ${CMAKE_DL_LIBS})
