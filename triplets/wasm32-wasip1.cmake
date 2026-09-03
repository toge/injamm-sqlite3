set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME WASI)
# wasi-sdk の場所は環境に合わせて解決する。
# VCPKG_CHAINLOAD_TOOLCHAIN_FILE がコマンドラインで明示されている場合は
# そちらが優先されるため、ここはフォールバックとしてのみ機能する。
if(DEFINED ENV{WASI_SDK_PATH} AND EXISTS "$ENV{WASI_SDK_PATH}/share/cmake/wasi-sdk-p1.cmake")
  set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{WASI_SDK_PATH}/share/cmake/wasi-sdk-p1.cmake")
elseif(EXISTS "/opt/wasi-sdk/share/cmake/wasi-sdk-p1.cmake")
  set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "/opt/wasi-sdk/share/cmake/wasi-sdk-p1.cmake")
elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/vm/wasi-sdk/share/cmake/wasi-sdk-p1.cmake")
  set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{HOME}/vm/wasi-sdk/share/cmake/wasi-sdk-p1.cmake")
endif()
