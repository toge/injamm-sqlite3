cmake_minimum_required(VERSION 3.20)
project(injamm-sqlite3 LANGUAGES C CXX)

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

find_package(unofficial-sqlite3 CONFIG QUIET)
if(NOT unofficial-sqlite3_FOUND)
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(STATUS "unofficial-sqlite3 not found (WASI): expecting sqlite3 amalgamation via SQLITE3_AMALGAMATION_DIR or FetchContent (INJAMM_SQLITE3_FETCH_AMALGAMATION)")
  else()
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
  endif()
endif()
# injamm (AppleClang) は USE_FMT=ON で fmt::fmt にリンクするが、
# injammConfig.cmake が find_dependency(fmt) を行わないため、
# 予め downstream 側で見つけておく必要がある。
find_package(fmt CONFIG QUIET)
find_package(injamm CONFIG REQUIRED)

option(BUILD_TEST "Build tests" ON)
option(INJAMM_SQLITE3_FETCH_AMALGAMATION "Fetch sqlite3 amalgamation (WASI only, when unofficial-sqlite3 is unavailable)" OFF)
set(SQLITE3_AMALGAMATION_DIR "" CACHE PATH "Directory containing sqlite3.c/sqlite3.h amalgamation (WASI only)")
set(SQLITE3_AMALGAMATION_VERSION "3530400" CACHE STRING "sqlite3 amalgamation version to fetch (WASI only)")
set(SQLITE3_AMALGAMATION_SHA256 "1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d" CACHE STRING "SHA256 of the sqlite3 amalgamation zip (WASI only)")

add_library(injamm-sqlite3 INTERFACE)
add_library(injamm::sqlite3 ALIAS injamm-sqlite3)
set_target_properties(injamm-sqlite3 PROPERTIES EXPORT_NAME sqlite3)

target_include_directories(injamm-sqlite3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(unofficial-sqlite3_FOUND)
  target_link_libraries(injamm-sqlite3 INTERFACE injamm::injamm unofficial::sqlite3::sqlite3)
else()
  # WASI: sqlite3 アマルガメーション (sqlite3.c) を単一ファイルコンパイルする。
  # vcpkg の sqlite3 ポートは wasip1 でビルド失敗する
  # (os_unix の UNIX_SHM_* が未定義になる) ため、WASI に限って amalgamation を使う。
  if(NOT SQLITE3_AMALGAMATION_DIR)
    if(INJAMM_SQLITE3_FETCH_AMALGAMATION)
      include(FetchContent)
      FetchContent_Declare(sqlite3_amalgamation
        URL "https://www.sqlite.org/2026/sqlite-amalgamation-${SQLITE3_AMALGAMATION_VERSION}.zip"
        URL_HASH "SHA256=${SQLITE3_AMALGAMATION_SHA256}"
      )
      FetchContent_MakeAvailable(sqlite3_amalgamation)
      set(SQLITE3_AMALGAMATION_DIR "${sqlite3_amalgamation_SOURCE_DIR}" CACHE PATH "Directory containing sqlite3.c/sqlite3.h amalgamation (WASI only)" FORCE)
    else()
      message(FATAL_ERROR "WASI build requires sqlite3 amalgamation: set SQLITE3_AMALGAMATION_DIR or -DINJAMM_SQLITE3_FETCH_AMALGAMATION=ON")
    endif()
  endif()
  if(NOT EXISTS "${SQLITE3_AMALGAMATION_DIR}/sqlite3.c")
    message(FATAL_ERROR "SQLITE3_AMALGAMATION_DIR does not contain sqlite3.c: ${SQLITE3_AMALGAMATION_DIR}")
  endif()
  add_library(injamm_sqlite3_amalgamation STATIC "${SQLITE3_AMALGAMATION_DIR}/sqlite3.c")
  # FetchContent 取得時はソースがビルドディレクトリ配下になるため、
  # ビルド時パスの漏洩を避けて BUILD/INSTALL を分離する。
  target_include_directories(injamm_sqlite3_amalgamation PUBLIC
    "$<BUILD_INTERFACE:${SQLITE3_AMALGAMATION_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/injamm-sqlite3-amalgamation>"
  )
  target_compile_definitions(injamm_sqlite3_amalgamation PUBLIC
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
  )
  target_link_libraries(injamm-sqlite3 INTERFACE injamm::injamm injamm_sqlite3_amalgamation)
endif()

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
if(NOT unofficial-sqlite3_FOUND)
  install(
    TARGETS injamm_sqlite3_amalgamation
    EXPORT injamm-sqlite3Targets
  )
  install(
    FILES "${SQLITE3_AMALGAMATION_DIR}/sqlite3.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/injamm-sqlite3-amalgamation
  )
endif()

install(
  EXPORT injamm-sqlite3Targets
  FILE injamm-sqlite3Targets.cmake
  NAMESPACE injamm::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/injamm-sqlite3
)

include(CMakePackageConfigHelpers)
set(INJAMM_SQLITE3_USE_UNOFFICIAL_SQLITE3 ${unofficial-sqlite3_FOUND})
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
