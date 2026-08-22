#pragma once

#include <injamm/sqlite3/concept.hpp>
#include <sqlite3.h>
#include <charconv>
#include <string>
#include <string_view>

namespace injamm::sqlite3 {

// 1 行の sqlite3 ステートメントを、キー（列名）で文字列値へアクセスできるビューとしてラップ
struct sqlite3_row_view {
  sqlite3_stmt* stmt_;

  explicit sqlite3_row_view(sqlite3_stmt* stmt) : stmt_(stmt) {}

  // 列名 key に対応する値を文字列で返す。見つからなければ空文字を返す。
  std::string find(std::string_view key) const {
    if (!stmt_) return "";
    int n = sqlite3_column_count(stmt_);
    for (int i = 0; i < n; ++i) {
      if (key == sqlite3_column_name(stmt_, i)) {
        auto t = sqlite3_column_type(stmt_, i);
        switch (t) {
          case SQLITE_INTEGER: {
            auto val = sqlite3_column_int64(stmt_, i);
            char buf[24];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_FLOAT: {
            auto val = sqlite3_column_double(stmt_, i);
            char buf[64];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_TEXT: {
            auto text = sqlite3_column_text(stmt_, i);
            auto len = sqlite3_column_bytes(stmt_, i);
            return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(len));
          }
          default:
            return "";
        }
      }
    }
    return "";
  }
};

// 複数行のクエリ結果を、sqlite3_row_view を要素とする範囲for文対応のコレクションとしてラップ
struct sqlite3_result {
  sqlite3_stmt* stmt_;
  mutable bool started_;

  explicit sqlite3_result(sqlite3_stmt* stmt) : stmt_(stmt), started_(false) {}

  // 反復要素の型（1 行ビュー）
  using value_type = sqlite3_row_view;

  struct sentinel {};
  struct iterator {
    sqlite3_stmt* stmt_;
    int rc_ = SQLITE_ROW;
    sqlite3_row_view current_;

    iterator& operator++() {
      if (!stmt_) { rc_ = SQLITE_DONE; return *this; }
      rc_ = sqlite3_step(stmt_);
      current_ = sqlite3_row_view{stmt_};
      return *this;
    }
    sqlite3_row_view const& operator*() const { return current_; }
    bool operator!=(sentinel) const { return rc_ == SQLITE_ROW; }
  };

  iterator begin() const {
    if (!started_) {
      started_ = true;
      int rc = sqlite3_step(stmt_);
      return iterator{stmt_, rc, sqlite3_row_view{stmt_}};
    }
    return iterator{nullptr, SQLITE_DONE, sqlite3_row_view{nullptr}};
  }
  sentinel end() const { return {}; }
};

} // namespace injamm::sqlite3
