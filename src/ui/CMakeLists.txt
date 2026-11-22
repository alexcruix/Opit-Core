add_library(opitcore_ui STATIC
  Theme.h
  Theme.cpp
  MainWindow.h
  MainWindow.cpp
  UnifiedSettingsWindow.h
  UnifiedSettingsWindow.cpp
  AdvancedModuleDialogs.h
  AdvancedModuleDialogs.cpp
  FanCurveEditorDialog.h
  FanCurveEditorDialog.cpp
)

target_include_directories(opitcore_ui
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(opitcore_ui
  PUBLIC
    opitcore_core
    opitcore_fans
    opitcore_hardware
    opitcore_metrics
    opitcore_power
    opitcore_protection
    opitcore_control
    opitcore_platform_windows
    opitcore_utils
    opitcore_ai
    dwmapi
    uxtheme
    comctl32
)
