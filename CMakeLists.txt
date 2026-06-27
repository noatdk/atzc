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
  src/harness_engine.cpp
  src/posix_harness_process.cpp
  src/local_converter.cpp)
target_include_directories(atzc-core PUBLIC include)
target_link_libraries(atzc-core PUBLIC Threads::Threads)

# libatzcclient — the socket client, shared by atzc, the fcitx5 addon, and
# (out of tree) modore's AtzcBackend. Implements the same atzc::Converter
# contract as LocalConverter.
add_library(atzcclient STATIC src/client.cpp)
target_include_directories(atzcclient PUBLIC include)

# atzcd — the relay daemon = atzc-core + a Unix-socket server.
add_executable(atzcd src/atzcd/main.cpp)
target_include_directories(atzcd PRIVATE include src)
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
