cmake_minimum_required(VERSION 3.25)
project(cabinet_status VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CABINET_STATUS_BUILD_TESTS "Build portable behavior tests" OFF)

if(WIN32)
  add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
endif()

add_library(civetweb STATIC third_party/civetweb/src/civetweb.c)
target_include_directories(civetweb PUBLIC third_party/civetweb/include)
target_compile_definitions(civetweb PRIVATE NO_SSL NO_CGI NO_FILES NO_CACHING USE_STACK_SIZE=102400 SOCKET_TIMEOUT_QUANTUM=100)
if(WIN32)
  target_link_libraries(civetweb PUBLIC ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(civetweb PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_library(cabinet_core STATIC
  src/status_state.cpp
  src/iidx_protocol.cpp
  src/config.cpp
  src/avs_capture.cpp
  src/game_support.cpp
  src/pe_imports.cpp
)
target_include_directories(cabinet_core PUBLIC src)

add_library(cabinet_http STATIC src/http_server.cpp)
target_include_directories(cabinet_http PUBLIC src)
target_link_libraries(cabinet_http PUBLIC cabinet_core civetweb)

if(WIN32)
  add_library(cabinet_windows STATIC src/game_identity.cpp src/iat_hooks.cpp)
  target_include_directories(cabinet_windows PUBLIC src third_party/spice-sdk)
  target_link_libraries(cabinet_windows PUBLIC cabinet_http psapi version)

  add_library(cabinet-status SHARED src/plugin.cpp src/exports.def)
  target_link_libraries(cabinet-status PRIVATE cabinet_windows)
  target_link_options(cabinet-status PRIVATE -static -static-libgcc -static-libstdc++)
  set_target_properties(cabinet-status PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cabinet-status"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
  )

  install(TARGETS cabinet-status RUNTIME DESTINATION . COMPONENT Runtime)
  install(FILES dist/cabinet-status.ini DESTINATION . COMPONENT Runtime)
endif()

if(CABINET_STATUS_BUILD_TESTS)
  enable_testing()

  add_executable(status-state-tests tests/status_state_tests.cpp)
  target_link_libraries(status-state-tests PRIVATE cabinet_core)
  add_test(NAME status-state COMMAND status-state-tests)

  add_executable(config-tests tests/config_tests.cpp)
  target_link_libraries(config-tests PRIVATE cabinet_core)
  add_test(NAME config COMMAND config-tests)

  add_executable(game-support-tests tests/game_support_tests.cpp)
  target_link_libraries(game-support-tests PRIVATE cabinet_core)
  add_test(NAME game-support COMMAND game-support-tests)

  add_executable(protocol-tests tests/protocol_tests.cpp)
  target_link_libraries(protocol-tests PRIVATE cabinet_core)
  add_test(NAME protocol COMMAND protocol-tests)

  add_executable(pe-imports-tests tests/pe_imports_tests.cpp)
  target_link_libraries(pe-imports-tests PRIVATE cabinet_core)
  add_test(NAME pe-imports COMMAND pe-imports-tests)

  add_executable(iat-slot-tests tests/iat_slot_tests.cpp)
  target_include_directories(iat-slot-tests PRIVATE src)
  add_test(NAME iat-slot COMMAND iat-slot-tests)

  add_executable(http-smoke-host tests/http_smoke_host.cpp)
  target_link_libraries(http-smoke-host PRIVATE cabinet_http)
endif()
