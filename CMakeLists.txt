cmake_minimum_required(VERSION 3.16)
project(atzc_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# atzc-core — the transport-agnostic conversion core: the Engine interface, the
# Session cache/prefetch model, the harness engine that drives the IME over the
# ATD line protocol, and an in-process LocalConverter. The only platform-specific
# piece is the harness process launch (posix_harness_process.cpp today; a native
# Windows launch can be added without touching the rest). Reusable behind the
# daemon (Linux) or linked directly in-process (e.g. modore on Windows).
add_library(atzc-core STATIC
  src/session.cpp
  src/session_engine.cpp
  src/harness_engine.cpp
  src/posix_harness_process.cpp
  src/local_converter.cpp)
target_include_directories(atzc-core PUBLIC include)
target_link_libraries(atzc-core PUBLIC Threads::Threads)

# libatzcclient — the socket client + the POSIX MakeConverter (a reconnecting
# client). Shared by atzc, the fcitx5 addon, and (out of tree) modore's Linux
# AtzcBackend. Implements the same atzc::Converter contract as LocalConverter.
add_library(atzcclient STATIC src/client.cpp src/frontend_posix.cpp)
target_include_directories(atzcclient PUBLIC include)

# atzcd — the relay daemon = atzc-core + a Unix-socket server. The Wine harness
# it drives is bundled in engine/ (scripts + native); ATZC_ENGINE_DIR points
# atzcd at it so a source build is self-contained (no --engine-dir, no external
# tree). A release/installed build overrides this to the packaged engine path.
# The harvested engine install + Wine stay user-provided (engine/.atzc, or
# AT_HOME).
add_executable(atzcd src/atzcd/main.cpp)
target_include_directories(atzcd PRIVATE include src)
target_compile_definitions(atzcd PRIVATE
  ATZC_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine")
target_link_libraries(atzcd PRIVATE atzc-core atzcclient Threads::Threads)

# atzc — test/diagnostic client.
add_executable(atzc src/cli/main.cpp)
target_link_libraries(atzc PRIVATE atzcclient)

install(TARGETS atzcd atzc RUNTIME DESTINATION bin)
install(TARGETS atzc-core atzcclient ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# fcitx5 addon — built only if fcitx5's CMake packages are present.
option(BUILD_FCITX5 "Build the fcitx5 input-method addon" ON)
if(BUILD_FCITX5)
  find_package(Fcitx5Core QUIET)
  if(Fcitx5Core_FOUND)
    add_subdirectory(fcitx5)
  else()
    message(STATUS "Fcitx5Core not found — skipping fcitx5 addon (install fcitx5 dev packages to enable)")
  endif()
endif()

# Dry-run tests — off by default (no engine needed; drive a mock harness).
option(BUILD_TESTS "Build the dry-run unit tests" OFF)
if(BUILD_TESTS)
  enable_testing()
  add_executable(session_engine_test tests/session_engine_test.cpp)
  target_link_libraries(session_engine_test PRIVATE atzc-core)
  target_include_directories(session_engine_test PRIVATE include)
  add_test(NAME session_engine_test COMMAND session_engine_test)

  add_executable(harness_candx_test tests/harness_candx_test.cpp)
  target_link_libraries(harness_candx_test PRIVATE atzc-core)
  target_include_directories(harness_candx_test PRIVATE include)
  add_test(NAME harness_candx_test COMMAND harness_candx_test)
endif()
