cmake_minimum_required(VERSION 3.16)
project(atzc_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# atzc-core — the conversion core behind the Engine interface: the Session
# cache/prefetch model, an in-process LocalConverter, and the platform's engine.
#   POSIX:   HarnessEngine drives a harvested engine under Wine via the subprocess
#            harness (bundled in engine/); MakeConverter is the socket client.
#   Windows: WindowsImeEngine drives the *installed* IME in-process via TSF;
#            MakeConverter wraps it in a LocalConverter (no daemon, no socket).
# modore consumes this in-process on Windows; on Linux it reaches atzcd via the
# socket client below.
add_library(atzc-core STATIC
  src/session.cpp
  src/local_converter.cpp)
if(WIN32)
  target_sources(atzc-core PRIVATE
    src/windows_ime_engine.cpp
    src/frontend_windows.cpp)
  target_link_libraries(atzc-core PUBLIC ole32 oleaut32)  # COM + BSTR (TSF GUIDs are local)
else()
  target_sources(atzc-core PRIVATE
    src/harness_engine.cpp
    src/posix_harness_process.cpp)
endif()
target_include_directories(atzc-core PUBLIC include)
target_link_libraries(atzc-core PUBLIC Threads::Threads)

install(TARGETS atzc-core ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# The socket client, relay daemon, and CLI are POSIX (Unix-domain sockets); the
# Windows host consumes atzc-core in-process (LocalConverter) instead.
if(NOT WIN32)
  # libatzcclient — the socket client + the POSIX MakeConverter (a reconnecting
  # client). Shared by atzc, the fcitx5 addon, and (out of tree) modore's Linux
  # AtzcBackend. Implements the same atzc::Converter contract as LocalConverter.
  add_library(atzcclient STATIC src/client.cpp src/frontend_posix.cpp)
  target_include_directories(atzcclient PUBLIC include)

  # atzcd — the relay daemon = atzc-core + a Unix-socket server. The Wine harness
  # it drives is bundled in engine/ (scripts + native); ATZC_ENGINE_DIR points
  # atzcd at it so a source build is self-contained (no --engine-dir, no external
  # tree). The harvested engine install + Wine stay user-provided (engine/.atzc,
  # or AT_HOME).
  add_executable(atzcd src/atzcd/main.cpp)
  target_include_directories(atzcd PRIVATE include src)
  target_compile_definitions(atzcd PRIVATE
    ATZC_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine")
  target_link_libraries(atzcd PRIVATE atzc-core atzcclient Threads::Threads)

  # atzc — test/diagnostic client.
  add_executable(atzc src/cli/main.cpp)
  target_link_libraries(atzc PRIVATE atzcclient)

  install(TARGETS atzcd atzc RUNTIME DESTINATION bin)
  install(TARGETS atzcclient ARCHIVE DESTINATION lib)
endif()

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
