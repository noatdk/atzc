cmake_minimum_required(VERSION 3.16)
project(atzc_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# libatzcclient — the socket client, shared by atzc, the fcitx5 addon, and
# (out of tree) modore's AtzcBackend.
add_library(atzcclient STATIC src/client.cpp)
target_include_directories(atzcclient PUBLIC include)

# atzcd — the relay daemon.
find_package(Threads REQUIRED)
add_executable(atzcd src/atzcd/main.cpp src/atzcd/harness_engine.cpp src/atzcd/session.cpp)
target_include_directories(atzcd PRIVATE include src)
target_link_libraries(atzcd PRIVATE atzcclient Threads::Threads)

# atzc — test/diagnostic client.
add_executable(atzc src/cli/main.cpp)
target_link_libraries(atzc PRIVATE atzcclient)

install(TARGETS atzcd atzc RUNTIME DESTINATION bin)
install(TARGETS atzcclient ARCHIVE DESTINATION lib)
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
