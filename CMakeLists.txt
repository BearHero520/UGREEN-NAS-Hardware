cmake_minimum_required(VERSION 3.20)

project(ugreenctl VERSION 0.2.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "ugreenctl directly accesses Linux x86 I/O ports and can only be built on Linux.")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
  message(FATAL_ERROR "ugreenctl currently supports x86/x86_64 UGREEN NAS hardware only.")
endif()

include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(ugreenctl_core STATIC
  core/loader.c
)
target_include_directories(ugreenctl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ugreenctl_core PRIVATE
  UGREENCTL_DEFAULT_MODEL_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/ugreenctl/models"
)
target_link_libraries(ugreenctl_core PRIVATE dl)
set_target_properties(ugreenctl_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ugreenctl_hardware STATIC
  superio/it86x.c
  fan/it8613_direct.c
  fan/it8613_hwmon.c
  fan/it8613_fan.c
  fan/it8613_dx4600.c
  fan/it8613_dxp4800.c
  fan/it8613_dxp4800s.c
  fan/it8613_dxp480t.c
  fan/it8613_dxp6800pro.c
  fan/thermal_curve.c
  power/it8613_power.c
  power/rtc_wake.c
  network/wol.c
  network/wol_dx4600.c
  network/wol_dxp4800.c
  network/wol_dxp4800plus.c
  network/wol_dxp4800s.c
  network/wol_dxp480tplus.c
  network/wol_dxp6800pro.c
  led/led.c
  ec/ec.c
)
target_include_directories(ugreenctl_hardware PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(ugreenctl_hardware PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(ugreenctl core/main.c)
target_include_directories(ugreenctl PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ugreenctl PRIVATE ugreenctl_core ugreenctl_hardware dl)

add_executable(ugreenctl-fand core/fan_daemon.c)
target_include_directories(ugreenctl-fand PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ugreenctl-fand PRIVATE ugreenctl_hardware)

foreach(target ugreenctl_core ugreenctl_hardware ugreenctl ugreenctl-fand)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

function(add_model_plugin target source)
  add_library(${target} MODULE ${source})
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(${target} PRIVATE ugreenctl_hardware)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/models"
  )
endfunction()

add_model_plugin(dx4600 models/dx4600.c)
add_model_plugin(dxp4800plus models/dxp4800plus.c)
add_model_plugin(dxp4800s models/dxp4800s.c)
add_model_plugin(dxp480tplus models/dxp480tplus.c)
add_model_plugin(dxp2800 models/unsupported_model.c)
target_compile_definitions(dxp2800 PRIVATE MODEL_ID="dxp2800" MODEL_NAME="UGREEN DXP2800" MODEL_DMI_NAME="DXP2800")
add_model_plugin(dxp4800 models/dxp4800.c)
add_model_plugin(dxp6800pro models/dxp6800pro.c)
add_model_plugin(dxp8800plus models/unsupported_model.c)
target_compile_definitions(dxp8800plus PRIVATE MODEL_ID="dxp8800plus" MODEL_NAME="UGREEN DXP8800 Plus" MODEL_DMI_NAME="DXP8800 Plus")

install(TARGETS ugreenctl ugreenctl-fand RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS dx4600 dxp2800 dxp4800 dxp4800plus dxp4800s dxp480tplus dxp6800pro dxp8800plus
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/ugreenctl/models
)
install(FILES
  README.md
  README.zh-CN.md
  docs/HARDWARE_SUPPORT.md
  docs/COMPATIBILITY.md
  docs/COMPATIBILITY.zh-CN.md
  docs/DXP4800S_REVERSE_ENGINEERING.md
  docs/DXP4800_REVERSE_ENGINEERING.md
  docs/DX4600_REVERSE_ENGINEERING.zh-CN.md
  docs/WOL_FIRMWARE_EVIDENCE.md
  docs/WOL_FIRMWARE_EVIDENCE.zh-CN.md
  docs/SCHEDULED_POWER_FIRMWARE_EVIDENCE.md
  docs/DXP4800S_REVERSE_ENGINEERING.zh-CN.md
  docs/DXP480T_PLUS_FAN_REVERSE_ENGINEERING.md
  docs/DXP6800_PRO_REVERSE_ENGINEERING.md
  docs/DXP4800S_INTAKE.md
  docs/DX4600_INTAKE.md
  docs/FAN_CURVE.md
  docs/NEW_MODEL_TEMPLATE.md
  models/compatibility.json
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

if(BUILD_TESTING)
  add_test(NAME cli-help COMMAND ugreenctl --help)
  add_executable(test-it8613-hwmon tests/test_it8613_hwmon.c)
  target_include_directories(test-it8613-hwmon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(test-it8613-hwmon PRIVATE ugreenctl_hardware)
  target_compile_options(test-it8613-hwmon PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-hwmon COMMAND test-it8613-hwmon)
  add_executable(test-it8613-direct
    tests/test_it8613_direct.c
    fan/it8613_direct.c
  )
  target_include_directories(test-it8613-direct PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-direct PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-direct COMMAND test-it8613-direct)
  add_executable(test-it8613-power
    tests/test_it8613_power.c
    power/it8613_power.c
  )
  target_include_directories(test-it8613-power PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-power PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-power COMMAND test-it8613-power)
  add_executable(test-it8613-dxp4800
    tests/test_it8613_dxp4800.c
    fan/it8613_dxp4800.c
  )
  target_include_directories(test-it8613-dxp4800 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-dxp4800 PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-dxp4800 COMMAND test-it8613-dxp4800)
  add_executable(test-it8613-dx4600
    tests/test_it8613_dx4600.c
    fan/it8613_dx4600.c
  )
  target_include_directories(test-it8613-dx4600 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-dx4600 PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-dx4600 COMMAND test-it8613-dx4600)
  add_executable(test-dx4600-plugin
    tests/test_dx4600_plugin.c
    models/dx4600.c
  )
  target_include_directories(test-dx4600-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-dx4600-plugin PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME dx4600-plugin COMMAND test-dx4600-plugin)
  add_executable(test-wol-dxp4800
    tests/test_wol_dxp4800.c
    network/wol_dx4600.c
    network/wol_dxp4800.c
    network/wol_dxp4800plus.c
    network/wol_dxp4800s.c
    network/wol_dxp480tplus.c
    network/wol_dxp6800pro.c
  )
  target_include_directories(test-wol-dxp4800 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-wol-dxp4800 PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME wol-dxp4800 COMMAND test-wol-dxp4800)
  add_executable(test-rtc-wake tests/test_rtc_wake.c)
  target_include_directories(test-rtc-wake PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(test-rtc-wake PRIVATE ugreenctl_hardware)
  target_compile_options(test-rtc-wake PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME rtc-wake COMMAND test-rtc-wake)
  add_executable(test-it8613-dxp480t
    tests/test_it8613_dxp480t.c
    fan/it8613_dxp480t.c
  )
  target_include_directories(test-it8613-dxp480t PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-dxp480t PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-dxp480t COMMAND test-it8613-dxp480t)
  add_executable(test-it8613-dxp6800pro
    tests/test_it8613_dxp6800pro.c
    fan/it8613_dxp6800pro.c
  )
  target_include_directories(test-it8613-dxp6800pro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(test-it8613-dxp6800pro PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME it8613-dxp6800pro COMMAND test-it8613-dxp6800pro)
  add_executable(test-thermal-curve tests/test_thermal_curve.c)
  target_include_directories(test-thermal-curve PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(test-thermal-curve PRIVATE ugreenctl_hardware)
  target_compile_options(test-thermal-curve PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME thermal-curve COMMAND test-thermal-curve)
endif()
