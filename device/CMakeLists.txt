cmake_minimum_required(VERSION 3.18)
project(asthmaguard VERSION 0.2.0 LANGUAGES C CXX)

# AsthmaGuard for the Raspberry Pi Zero 2 W (Raspberry Pi OS Bookworm, 64-bit).
#   cmake -S device -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   ctest --test-dir build --output-on-failure
# -DAG_DAEMON=OFF builds only the logic + model tests (no libsystemd/ALSA needed).

option(AG_DAEMON "Build the asthmaguardd daemon (needs libsystemd + ALSA)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Same float behaviour as the training features (ml/features_lib.py builds
# the shared feature code with -ffp-contract=off).
add_compile_options(-ffp-contract=off)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ---------------------------------------------------------------- pure logic
add_library(ag_core STATIC
  src/core/types.cpp src/core/config.cpp src/core/detector.cpp
  src/core/state_machine.cpp src/core/eventlog.cpp src/core/sim.cpp
  src/core/button.cpp src/core/core.cpp
  src/core/sensors/motion.cpp src/core/sensors/ppg_algo.cpp src/core/sensors/features.cpp)
target_include_directories(ag_core PUBLIC src/core ${TP})
target_compile_options(ag_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- TFLite Micro
file(GLOB_RECURSE TFLM_SRC CONFIGURE_DEPENDS ${TP}/tflm/tensorflow/*.cc)
add_library(tflm STATIC ${TFLM_SRC})
target_include_directories(tflm SYSTEM PUBLIC ${TP}/tflm ${TP}/tflm/third_party/flatbuffers/include)
target_compile_definitions(tflm PUBLIC TF_LITE_STATIC_MEMORY)
target_compile_options(tflm PRIVATE -O2 -w)

# ---------------------------------------------------------------- cough model
add_library(ag_model STATIC model/model_data.cc src/hw/cough_model.cpp)
target_include_directories(ag_model PUBLIC model src/hw)
target_link_libraries(ag_model PUBLIC ag_core tflm)

# ---------------------------------------------------------------- tests
enable_testing()
add_library(unity STATIC ${TP}/unity/unity.c)
target_include_directories(unity PUBLIC ${TP}/unity)

add_executable(test_logic tests/test_logic.cpp)
target_link_libraries(test_logic ag_core unity m)
add_test(NAME logic COMMAND test_logic)

add_executable(test_model tests/test_model.cpp)
target_link_libraries(test_model ag_model unity m)
add_test(NAME model COMMAND test_model)

# ---------------------------------------------------------------- daemon
if(AG_DAEMON)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
  pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
  find_package(Threads REQUIRED)
  add_executable(asthmaguardd
    src/main.cpp src/actuators.cpp src/storage.cpp src/console.cpp
    src/hw/i2c.cpp src/hw/mpu6050.cpp src/hw/max30102.cpp src/hw/gpio.cpp
    src/hw/pwm.cpp src/hw/audio_alsa.cpp src/ble/bluez_gatt.cpp)
  target_include_directories(asthmaguardd PRIVATE src)
  target_compile_options(asthmaguardd PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_compile_definitions(asthmaguardd PRIVATE AG_VERSION="${PROJECT_VERSION}")
  target_link_libraries(asthmaguardd ag_core ag_model PkgConfig::SYSTEMD PkgConfig::ALSA Threads::Threads m)
  install(TARGETS asthmaguardd RUNTIME DESTINATION bin)
  install(FILES deploy/asthmaguard.service DESTINATION /etc/systemd/system)
endif()
