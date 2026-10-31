cmake_minimum_required(VERSION 3.20)
project(dji_rc_gamepad LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  message(FATAL_ERROR "MSVC is the only supported toolchain here")
endif()

# Chinese ANSI locale: force the compiler to read sources and literals as UTF-8.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/utf-8 /W4 /EHsc /permissive- /Zc:__cplusplus /Gw)
# Vendored ViGEm headers carry Latin-1 bytes in comments; harmless, but noisy.
add_compile_options(/wd4828)
add_link_options(/SUBSYSTEM:CONSOLE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Static CRT: the exe must be copy-able to another PC for cross-checking without
# installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- vendored ViGEmClient (static) ----
add_library(ViGEmClient STATIC
  third_party/ViGEmClient/src/ViGEmClient.cpp)
target_include_directories(ViGEmClient PUBLIC third_party/ViGEmClient/include)
target_include_directories(ViGEmClient PRIVATE third_party/ViGEmClient/src)
target_link_libraries(ViGEmClient PUBLIC setupapi.lib)

# ---- core library: protocol + serial + mapping ----
add_library(bridge_core STATIC
  src/BulkProbe.cpp
  src/Crc.cpp
  src/Config.cpp
  src/Duml.cpp
  src/LibUsbProbe.cpp
  src/Link.cpp
  src/Mapping.cpp
  src/RcState.cpp
  src/Serial.cpp
  src/Util.cpp
  src/Log.cpp)
target_include_directories(bridge_core PUBLIC src third_party/ViGEmClient/include)
target_link_libraries(bridge_core PUBLIC setupapi.lib cfgmgr32.lib)

# ---- self test: proves the virtual Xbox pad path with no hardware attached ----
add_executable(pad-selftest src/selftest.cpp)
target_link_libraries(pad-selftest PRIVATE bridge_core ViGEmClient)

# ---- main bridge ----
add_executable(dji-rc-bridge src/main.cpp)
target_include_directories(dji-rc-bridge PRIVATE src)
target_link_libraries(dji-rc-bridge PRIVATE bridge_core ViGEmClient user32.lib winmm.lib)

# ---- offline tests ----
enable_testing()
add_executable(bridge-tests tests/test_bridge.cpp)
target_include_directories(bridge-tests PRIVATE src)
target_link_libraries(bridge-tests PRIVATE bridge_core)
add_test(NAME bridge-tests COMMAND bridge-tests)
