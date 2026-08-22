cmake_minimum_required(VERSION 3.20)
project(injamm-sqlite3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Threads must be found before sqlite3 config tries find_dependency(Threads).
# CMake 3.30 FindThreads has a bug with Clang on Linux (fixed in CMake 3.31+).
find_package(Threads QUIET)
if(NOT Threads_FOUND)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Threads not detected by FindThreads (known CMake 3.30 + Clang issue), using fallback")
  endif()
  set(Threads_FOUND TRUE)
  set(CMAKE_THREAD_LIBS_INIT "pthread")
  add_library(Threads::Threads INTERFACE IMPORTED)
  set_target_properties(Threads::Threads PROPERTIES
    INTERFACE_LINK_LIBRARIES "pthread"
    INTERFACE_COMPILE_OPTIONS "-pthread")
endif()

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(injamm CONFIG REQUIRED)

option(BUILD_TEST "Build tests" ON)

add_library(injamm-sqlite3 INTERFACE)
add_library(injamm::sqlite3 ALIAS injamm-sqlite3)
set_target_properties(injamm-sqlite3 PROPERTIES EXPORT_NAME sqlite3)

target_include_directories(injamm-sqlite3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(injamm-sqlite3 INTERFACE injamm::injamm unofficial::sqlite3::sqlite3)

# ---- Install ----
include(GNUInstallDirs)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)

install(
  TARGETS injamm-sqlite3
  EXPORT injamm-sqlite3Targets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT injamm-sqlite3Targets
  FILE injamm-sqlite3Targets.cmake
  NAMESPACE injamm::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/injamm-sqlite3
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/injamm-sqlite3Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/injamm-sqlite3Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/injamm-sqlite3
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/injamm-sqlite3Config.cmake
  DESTINATION ${CMAKE_INSTALL_DATADIR}/injamm-sqlite3
)

# ---- Tests ----
if(BUILD_TEST)
  enable_testing()
  add_subdirectory(tests)
endif()
