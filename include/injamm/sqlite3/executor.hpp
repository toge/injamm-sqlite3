#pragma once

/**
 * @file executor.hpp
 * @brief injamm-sqlite3 用バイトコード VM エグゼキュータ
 *
 * @note このファイルは `include/injamm/bytecode_exec.hpp` の thin wrapper です。
 *       実行時ディスパッチ（sqlite3_row_view / sqlite3_result）は
 *       本体側の `runtime_field_accessible` / `forward_iterable` 分岐で対応します。
 *
 * @note このファイルは `injamm::detail` の名前空間エイリアスのみを提供する。
 *       `bc_execute` 等の実際の定義は `injamm/bytecode_exec.hpp` にあり、
 *       呼び出し側（engine.hpp 等）が個別にインクルードすること。
 */

namespace injamm::sqlite3::detail {

using namespace injamm::detail;

} // namespace injamm::sqlite3::detail
