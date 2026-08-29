#pragma once

#include <injamm/bytecode.hpp>
#include <injamm/sqlite3/concept.hpp>
#include <injamm/sqlite3/executor.hpp>
#include <injamm/bytecode_exec.hpp>
#include <injamm/bytecode_compile.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace injamm::sqlite3 {

// sqlite3 の行ビュー/結果セット向けランタイムレンダリングエンジン。
// T はキーアクセス可能、または（要素がキーアクセス可能な）前方反復可能コンテナ。
// 本体 injamm::engine<T> の ConstMap / partials コンストラクタに追従 (2026-08-29 時点)。
template <class T>
  requires runtime_field_accessible<T> || (forward_iterable<T> && runtime_field_accessible<typename T::value_type>)
class runtime_engine {
  injamm::detail::bytecode bc_;

public:
  // テンプレートをコンパイルしてバイトコードを構築する
  explicit runtime_engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false)
    : bc_(injamm::detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  // ConstMap 経由の @var 展開付き構築 — 本体 engine<T>(tmpl, consts) に追従
  template <class ConstMap>
  explicit runtime_engine(std::string_view tmpl, ConstMap const& consts, bool trim_blocks = false,
                          bool lstrip_blocks = false)
      : bc_(injamm::detail::bc_compile<T>(tmpl, consts, trim_blocks, lstrip_blocks)) {}

  // partial レジストリ付き構築 — 本体 engine<T>(tmpl, partials) に追従 ({{> name}} 用)
  explicit runtime_engine(std::string_view tmpl, std::vector<injamm::detail::partial_entry> partials,
                          bool trim_blocks = false, bool lstrip_blocks = false)
      : bc_(injamm::detail::bc_compile<T>(tmpl, std::move(partials), trim_blocks, lstrip_blocks)) {}

  // 値をレンダリングし、結果文字列を返す
  [[nodiscard]] std::expected<std::string, injamm::error_ctx> render(T const& value) const {
    return detail::bc_execute(bc_, value);
  }

  // 値をレンダリングし、out をクリアした上で結果文字列を書き込む（本体 bc_execute_into の仕様）
  [[nodiscard]] std::expected<void, injamm::error_ctx> render(T const& value, std::string& out) const {
    return detail::bc_execute_into(bc_, value, out);
  }
};

} // namespace injamm::sqlite3
