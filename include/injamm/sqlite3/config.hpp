#pragma once

/**
 * @file config.hpp
 * @brief injamm-sqlite3 のエラーハンドリングポリシー。
 *
 * frozenchars 流儀に従う:
 * - コンパイル時 (consteval) の不正入力はコンパイルエラーになる（例外を許容）。
 * - ランタイムの失敗は `std::expected<T, injamm::error_ctx>` で返る。
 * - ランタイムでの例外送出は禁止。
 *
 * 本ライブラリは injamm の `error_ctx` を再利用する。
 * `-fno-exceptions` でもコンパイル可能（WASI 対応）。
 */

#include <injamm/types.hpp>
#include <expected>

namespace injamm::sqlite3 {

/** @brief エラーコンテキスト型（injamm と共通） */
using injamm::error_ctx;

/** @brief 結果型エイリアス（injamm と共通） */
template <class T>
using expected = std::expected<T, error_ctx>;

} // namespace injamm::sqlite3
