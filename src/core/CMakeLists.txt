add_library(opitcore_core STATIC
  IModule.h
  Logger.h
  Logger.cpp
  Config.h
  Config.cpp
  DatabaseManager.h
  DatabaseManager.cpp
  HardwareKnowledgeBase.h
  HardwareKnowledgeBase.cpp
  UnifiedSettingsModel.h
  UnifiedSettingsModel.cpp
  ModuleAdvancedSettings.h
  ModuleAdvancedSettings.cpp
  NotificationManager.h
  NotificationManager.cpp
  CoreRuntime.h
  CoreRuntime.cpp
  # winsqlite3.dll so existe no Windows 10+. O SQLite vem embutido no binario para
  # o programa abrir em Windows 7/8.1 sem depender de um DLL do sistema.
  ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c
)

target_include_directories(opitcore_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(opitcore_core
  PUBLIC
    opitcore_security
    opitcore_fans
    opitcore_utils
    opitcore_hardware
    opitcore_metrics
    opitcore_power
    opitcore_protection
    opitcore_control
    opitcore_platform_windows
    opitcore_adaptive
    opitcore_ai
)
