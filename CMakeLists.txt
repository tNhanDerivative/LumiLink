cmake_minimum_required(VERSION 3.20)
project(LumiLink VERSION 0.1.0 LANGUAGES CXX)

option(LUMILINK_TESTS "Build the test executables" ON)
option(LUMILINK_VERBOSE "Emit info-level log messages (errors are always emitted)" ON)
set(LUMILINK_PAL "mock" CACHE STRING "Bluetooth PAL implementation: mock | native")
set_property(CACHE LUMILINK_PAL PROPERTY STRINGS mock native)

# Executables and shared libraries side by side, so tests find the DLL on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

function(lumilink_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /EHsc /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# PAL: the build system picks the implementation files, function bodies never
# contain platform #ifdefs.
# -----------------------------------------------------------------------------
add_library(lumilink_pal STATIC pal/IBluetoothDevice.cpp)
target_include_directories(lumilink_pal PUBLIC pal)
target_compile_features(lumilink_pal PUBLIC cxx_std_17)
set_target_properties(lumilink_pal PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  OBJCXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
lumilink_warnings(lumilink_pal)

if(LUMILINK_PAL STREQUAL "mock")
  target_sources(lumilink_pal PRIVATE pal/mock/MockBluetoothDevice.cpp)
elseif(LUMILINK_PAL STREQUAL "native")
  # Helpers shared by every native implementation (GATT UUIDs, receive buffer)
  target_include_directories(lumilink_pal PRIVATE pal/common)
  if(WIN32)
    # WinRT (C++/WinRT headers ship with the Windows 10 SDK >= 10.0.17763)
    target_sources(lumilink_pal PRIVATE pal/windows/BluetoothDeviceWin.cpp)
    target_link_libraries(lumilink_pal PRIVATE WindowsApp)
    target_compile_options(lumilink_pal PRIVATE /bigobj)
  elseif(APPLE)
    # CoreBluetooth, Objective-C++ with ARC
    enable_language(OBJCXX)
    target_sources(lumilink_pal PRIVATE pal/macos/BluetoothDeviceMac.mm)
    target_compile_options(lumilink_pal PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(lumilink_pal PRIVATE "-framework Foundation" "-framework CoreBluetooth")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # BlueZ over D-Bus, through sd-bus (package libsystemd-dev / systemd-devel)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    target_sources(lumilink_pal PRIVATE pal/linux/BluetoothDeviceLinux.cpp)
    target_link_libraries(lumilink_pal PRIVATE PkgConfig::SYSTEMD)
  else()
    message(FATAL_ERROR "No native Bluetooth PAL for ${CMAKE_SYSTEM_NAME}. Configure with -DLUMILINK_PAL=mock.")
  endif()
else()
  message(FATAL_ERROR "Unknown LUMILINK_PAL '${LUMILINK_PAL}' (expected mock or native)")
endif()

# -----------------------------------------------------------------------------
# C++ Core + Public C++ API: static library, consumed directly by C++ clients.
# -----------------------------------------------------------------------------
add_library(lumilink_core STATIC
  core/BTManager.cpp
  core/Command.cpp
  core/DeviceManager.cpp
  core/GroupManager.cpp
  core/LightGroup.cpp
  core/Log.cpp
  core/LumiLink.cpp
  core/ManagerFactory.cpp
  core/Packet.cpp
  core/RawManager.cpp
  core/TextManager.cpp)
target_include_directories(lumilink_core PUBLIC include PRIVATE core)
target_link_libraries(lumilink_core PRIVATE lumilink_pal)
target_compile_features(lumilink_core PUBLIC cxx_std_17)
target_compile_definitions(lumilink_core PRIVATE LUMILINK_VERBOSE=$<BOOL:${LUMILINK_VERBOSE}>)
set_target_properties(lumilink_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
lumilink_warnings(lumilink_core)

# -----------------------------------------------------------------------------
# Public C API + platform boundary: the shared library. Only ll_* is exported,
# every C++ symbol stays hidden.
# -----------------------------------------------------------------------------
add_library(lumilink SHARED capi/LumiLinkCAPI.cpp)
target_include_directories(lumilink PUBLIC include PRIVATE core)
target_link_libraries(lumilink PRIVATE lumilink_core)
target_compile_definitions(lumilink PRIVATE LUMILINK_BUILDING)
set_target_properties(lumilink PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
lumilink_warnings(lumilink)

# On macOS, executables that use Bluetooth must carry NSBluetoothAlwaysUsageDescription.
# Command line tools have no bundle, so the Info.plist is embedded in the binary.
function(lumilink_bluetooth_executable target)
  if(APPLE AND LUMILINK_PAL STREQUAL "native")
    target_link_options(${target} PRIVATE
      "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/pal/macos/Info.plist")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# Interactive command line, to drive a device by hand.
# -----------------------------------------------------------------------------
add_executable(interactive tools/interactive.cpp)
target_link_libraries(interactive PRIVATE lumilink_core)
lumilink_warnings(interactive)
lumilink_bluetooth_executable(interactive)

# -----------------------------------------------------------------------------
# Tests: one executable per suite, run with ctest.
# -----------------------------------------------------------------------------
if(LUMILINK_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)

  # Suites testing core internals link the static libraries directly.
  function(lumilink_core_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE core tests)
    target_link_libraries(${name} PRIVATE lumilink_core lumilink_pal Threads::Threads)
    target_compile_definitions(${name} PRIVATE
      LUMILINK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    lumilink_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  # Same suite on every platform, against whichever PAL was built. Links the PAL only.
  # With LUMILINK_PAL=native it needs a powered LumiLink device in range.
  add_executable(pal_tests tests/pal/BluetoothDeviceTests.cpp)
  target_include_directories(pal_tests PRIVATE tests)
  target_link_libraries(pal_tests PRIVATE lumilink_pal)
  lumilink_warnings(pal_tests)
  lumilink_bluetooth_executable(pal_tests)
  add_test(NAME pal_tests COMMAND pal_tests)

  lumilink_core_test(packet_tests tests/core/PacketTests.cpp)
  lumilink_core_test(group_tests tests/core/GroupTests.cpp)
  lumilink_core_test(golden_raw_tests tests/core/GoldenRawTests.cpp)
  # These suites drive BTManager and expect the in-memory device
  if(LUMILINK_PAL STREQUAL "mock")
    lumilink_core_test(thread_tests tests/core/ThreadTests.cpp)
    lumilink_core_test(btmanager_tests tests/core/BTManagerTests.cpp)
  endif()

  # The C API suite only sees what a foreign client sees: the header and the shared library.
  add_executable(capi_tests tests/capi/CApiTests.cpp)
  target_include_directories(capi_tests PRIVATE tests)
  target_link_libraries(capi_tests PRIVATE lumilink)
  target_compile_definitions(capi_tests PRIVATE
    LUMILINK_MOCK_PAL=$<STREQUAL:${LUMILINK_PAL},mock>)
  lumilink_warnings(capi_tests)
  add_test(NAME capi_tests COMMAND capi_tests)
endif()
