# ---- WASI (wasip1) スモークテスト ----
# Catch2 が wasip1 でビルド不可 (signal 未対応) のため、
# 親 injamm の test_wasi_minimal と同様に Catch2 非依存のスモークテストにする。
# sqlite3 は injamm-sqlite3 が内包する amalgamation を使う。
# 常時ビルド可能 (hosted 依存なし) なので先に定義する。
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  add_executable(injamm-sqlite3_test_wasi test_wasi_sqlite3.cpp)
  target_link_libraries(injamm-sqlite3_test_wasi PRIVATE injamm-sqlite3)
  # wasm バイナリは直接実行できないため、wasmedge 経由で実行する
  find_program(WASMEDGE_EXECUTABLE wasmedge)
  if(WASMEDGE_EXECUTABLE)
    add_test(NAME injamm-sqlite3_test_wasi COMMAND ${WASMEDGE_EXECUTABLE} injamm-sqlite3_test_wasi)
  else()
    message(STATUS "wasmedge not found: injamm-sqlite3_test_wasi will not run under ctest (run manually: wasmedge ./injamm-sqlite3_test_wasi)")
  endif()
  # WASI ビルドでは hosted なテスト/ベンチはスキップ
  return()
endif()

# ---- Catch2 (Emscripten では amalgamated 版を再コンパイル) ----
if(EMSCRIPTEN)
  # Emscripten: vcpkg がインストールした Catch2 静的ライブラリは
  # -sNO_DISABLE_EXCEPTION_CATCHING なしでコンパイルされているため、
  # Catch2 の例外翻訳関数が正しく動作しない。
  # ここでは Catch2 ソースをアマルガメーションファイル経由で再コンパイルする。
  include(FetchContent)
  FetchContent_Declare(
    catch2_emscripten
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/catch2_emscripten-src
  )
  cmake_policy(SET CMP0169 OLD)
  FetchContent_GetProperties(catch2_emscripten)
  if(NOT catch2_emscripten_POPULATED)
    FetchContent_Populate(catch2_emscripten)
  endif()
  cmake_policy(SET CMP0169 NEW)
  add_library(catch2_emscripten_core STATIC
    "${catch2_emscripten_SOURCE_DIR}/extras/catch_amalgamated.cpp"
  )
  target_include_directories(catch2_emscripten_core
    BEFORE PUBLIC
      "${catch2_emscripten_SOURCE_DIR}/extras"
  )
  target_compile_options(catch2_emscripten_core
    PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING"
  )
  target_compile_features(catch2_emscripten_core PRIVATE cxx_std_17)
  target_link_options(catch2_emscripten_core
    INTERFACE "-sNO_DISABLE_EXCEPTION_CATCHING"
  )
  set(_catch2_targets catch2_emscripten_core)
  set(_need_custom_main TRUE)
else()
  find_package(Catch2 CONFIG REQUIRED)
  include(Catch)
  set(_catch2_targets Catch2::Catch2 Catch2::Catch2WithMain)
  set(_need_custom_main FALSE)
endif()

# Mock test (no real SQLite3 needed)
add_executable(injamm-sqlite3_mock_test test_mock.cpp)
if(_need_custom_main)
  target_sources(injamm-sqlite3_mock_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/em_catch2_main.cpp)
endif()
target_link_libraries(injamm-sqlite3_mock_test PRIVATE injamm-sqlite3 ${_catch2_targets})
if(EMSCRIPTEN)
  target_compile_options(injamm-sqlite3_mock_test PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING")
else()
  catch_discover_tests(injamm-sqlite3_mock_test)
endif()
add_test(NAME injamm-sqlite3_mock_test COMMAND injamm-sqlite3_mock_test)

# Real sqlite3 test (requires system SQLite3, not available on Emscripten)
if(NOT EMSCRIPTEN)
  find_package(SQLite3 REQUIRED)
  add_executable(injamm-sqlite3_test test_sqlite3.cpp)
  target_link_libraries(injamm-sqlite3_test PRIVATE injamm-sqlite3 Catch2::Catch2WithMain SQLite3::SQLite3)
  catch_discover_tests(injamm-sqlite3_test)
  add_test(NAME injamm-sqlite3_test COMMAND injamm-sqlite3_test)

  # Dispatch benchmark (manual run, not registered with ctest)
  add_executable(injamm-sqlite3_bench bench_dispatch.cpp)
  target_link_libraries(injamm-sqlite3_bench PRIVATE injamm-sqlite3 SQLite3::SQLite3)

  # Container vs direct comparison benchmark (Catch2 BENCHMARK)
  add_executable(injamm-sqlite3_bench_vs bench_sqlite_vs.cpp)
  target_link_libraries(injamm-sqlite3_bench_vs PRIVATE injamm-sqlite3 Catch2::Catch2WithMain SQLite3::SQLite3)
  # Bench binary is run manually: ./injamm-sqlite3_bench_vs  or  ctest -R bench_vs -V
  catch_discover_tests(injamm-sqlite3_bench_vs)
  add_test(NAME injamm-sqlite3_bench_vs COMMAND injamm-sqlite3_bench_vs)
endif()
