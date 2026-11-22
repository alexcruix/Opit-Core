cmake_minimum_required(VERSION 3.16)
enable_testing()

macro(add_opit_test name srcFile)
  add_executable(${name} ${srcFile})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
  if(MSVC)
    target_compile_options(${name} PRIVATE /guard:cf)
    target_link_options(${name} PRIVATE /GUARD:CF)
  endif()
  target_link_libraries(${name} PRIVATE
    opitcore_security
    opitcore_core
    opitcore_fans
    opitcore_adaptive
    opitcore_hardware
    opitcore_metrics
    opitcore_power
    opitcore_protection
    opitcore_control
    opitcore_platform_windows
    opitcore_utils
  )
  add_test(NAME ${name} COMMAND ${name})
endmacro()

add_opit_test(test_config test_config.cpp)
add_opit_test(test_hardware test_hardware.cpp)
add_opit_test(test_power test_power.cpp)
add_opit_test(test_metrics test_metrics.cpp)
add_opit_test(test_protection test_protection.cpp)
add_opit_test(test_control test_control.cpp)
add_opit_test(test_process test_process.cpp)
add_opit_test(test_process_governor test_process_governor.cpp)
add_opit_test(test_service_governor test_service_governor.cpp)
add_opit_test(test_database test_database.cpp)
add_opit_test(test_unified_settings test_unified_settings.cpp)
add_opit_test(test_adaptive_engine test_adaptive_engine.cpp)
add_opit_test(test_orchestrator_execution test_orchestrator_execution.cpp)
add_opit_test(test_mode_guardian test_mode_guardian.cpp)
add_opit_test(test_plan_bands test_plan_bands.cpp)
add_opit_test(test_advanced_module_settings test_advanced_module_settings.cpp)
add_opit_test(test_fans_module test_fans_module.cpp)
add_opit_test(test_lhm_bridge test_lhm_bridge.cpp)
add_dependencies(test_lhm_bridge opitcore_lhm_bridge OpitCore)
add_opit_test(test_disk_protection_and_logging test_disk_protection_and_logging.cpp)
add_opit_test(test_opit_security test_opit_security.cpp)
add_opit_test(test_adversarial_suite test_adversarial_suite.cpp)
add_opit_test(test_gpu_and_game_profiles test_gpu_and_game_profiles.cpp)

add_executable(test_ui_responsiveness test_ui_responsiveness.cpp)
target_include_directories(test_ui_responsiveness PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(test_ui_responsiveness PRIVATE /guard:cf)
  target_link_options(test_ui_responsiveness PRIVATE /GUARD:CF)
endif()
target_link_libraries(test_ui_responsiveness PRIVATE
  opitcore_ui
  opitcore_core
  opitcore_fans
  opitcore_hardware
  opitcore_metrics
  opitcore_power
  opitcore_protection
  opitcore_control
  opitcore_platform_windows
  opitcore_utils
  dwmapi
  uxtheme
  comctl32
)
add_test(NAME test_ui_responsiveness COMMAND test_ui_responsiveness)
