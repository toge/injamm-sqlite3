#pragma once

#include <injamm/sqlite3/concept.hpp>
#include <sqlite3.h>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace injamm::sqlite3 {

// 1 行の sqlite3 ステートメントを、キー（列名）で文字列値へアクセスできるビューとしてラップ
// 内部で列名→index のキャッシュを持ち、TEXT 列は sqlite の内部バッファを
// string_view で直接参照することでヒープ確保を回避する。
struct sqlite3_row_view {
  sqlite3_stmt* stmt_ = nullptr;
  // sqlite3_result から共有される列名キャッシュ（所有は result 側）
  std::vector<std::string_view> const* shared_names_ = nullptr;
  // 単独利用時のローカルキャッシュ（最初の find で構築、行ごとに 1 回）
  mutable std::vector<std::string_view> local_names_;
  mutable int local_ncols_ = -1;
  // INTEGER/FLOAT を文字列化する際の作業バッファ（行ごとに独立）
  mutable char num_buf_[64] = {};

  explicit sqlite3_row_view(sqlite3_stmt* stmt) : stmt_(stmt) {}
  explicit sqlite3_row_view(sqlite3_stmt* stmt, std::vector<std::string_view> const* shared)
      : stmt_(stmt), shared_names_(shared) {}

  // 列名 key に対応する値を文字列ビューで返す。見つからなければ空ビューを返す。
  // TEXT は sqlite 内部バッファを直接参照、INTEGER/FLOAT は to_chars で
  // num_buf_ に書き出して返す（呼び出しごとに上書き、即時消費前提）。
  std::string_view find(std::string_view key) const {
    if (!stmt_) return {};
    int idx = -1;
    // --- 列インデックス解決（キャッシュ利用） ---
    if (shared_names_) {
      // 共有キャッシュ（sqlite3_result 経由）: O(n) 線形探索だが
      // sqlite3_column_name の C 呼び出しは発生しない
      for (int i = 0; i < static_cast<int>(shared_names_->size()); ++i) {
        if ((*shared_names_)[i] == key) {
          idx = i;
          break;
        }
      }
    } else {
      // ローカルキャッシュ（単一行利用時）
      if (local_ncols_ == -1) {
        local_ncols_ = sqlite3_column_count(stmt_);
        local_names_.reserve(static_cast<std::size_t>(local_ncols_));
        for (int i = 0; i < local_ncols_; ++i) {
          auto* n = sqlite3_column_name(stmt_, i);
          local_names_.emplace_back(n ? std::string_view{n} : std::string_view{});
        }
      }
      for (int i = 0; i < local_ncols_; ++i) {
        if (local_names_[i] == key) {
          idx = i;
          break;
        }
      }
    }
    if (idx == -1) return {};
    auto t = sqlite3_column_type(stmt_, idx);
    switch (t) {
      case SQLITE_INTEGER: {
        auto val = sqlite3_column_int64(stmt_, idx);
        auto [p, ec] = std::to_chars(num_buf_, num_buf_ + sizeof(num_buf_), val);
        return std::string_view{num_buf_, static_cast<std::size_t>(p - num_buf_)};
      }
      case SQLITE_FLOAT: {
        auto val = sqlite3_column_double(stmt_, idx);
        auto [p, ec] = std::to_chars(num_buf_, num_buf_ + sizeof(num_buf_), val);
        return std::string_view{num_buf_, static_cast<std::size_t>(p - num_buf_)};
      }
      case SQLITE_TEXT: {
        auto text = sqlite3_column_text(stmt_, idx);
        auto len = sqlite3_column_bytes(stmt_, idx);
        if (!text) return {};
        return std::string_view{reinterpret_cast<const char*>(text), static_cast<std::size_t>(len)};
      }
      default:
        return {};
    }
  }
};

// 複数行のクエリ結果を、sqlite3_row_view を要素とする範囲for文対応のコレクションとしてラップ
// 列名キャッシュをクエリ単位で 1 回だけ構築し、各行の row_view に共有する
struct sqlite3_result {
  sqlite3_stmt* stmt_ = nullptr;
  mutable bool started_ = false;
  // 列名キャッシュ（クエリ単位で共有、遅延構築）
  mutable std::vector<std::string_view> col_names_;
  mutable bool col_names_cached_ = false;

  explicit sqlite3_result(sqlite3_stmt* stmt) : stmt_(stmt), started_(false) {}

  // 列名キャッシュを取得（必要なら構築）
  std::vector<std::string_view> const* col_names_ptr() const {
    if (!col_names_cached_) {
      if (!stmt_) return &col_names_;
      int n = sqlite3_column_count(stmt_);
      col_names_.reserve(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        auto* nm = sqlite3_column_name(stmt_, i);
        col_names_.emplace_back(nm ? std::string_view{nm} : std::string_view{});
      }
      col_names_cached_ = true;
    }
    return &col_names_;
  }

  // 反復要素の型（1 行ビュー）
  using value_type = sqlite3_row_view;

  struct sentinel {};
  struct iterator {
    sqlite3_stmt* stmt_ = nullptr;
    int rc_ = SQLITE_ROW;
    sqlite3_row_view current_;
    std::vector<std::string_view> const* shared_names_ = nullptr;

    iterator() = default;
    iterator(sqlite3_stmt* stmt, int rc, sqlite3_row_view cur, std::vector<std::string_view> const* shared)
        : stmt_(stmt), rc_(rc), current_(std::move(cur)), shared_names_(shared) {}

    iterator& operator++() {
      if (!stmt_) {
        rc_ = SQLITE_DONE;
        return *this;
      }
      rc_ = sqlite3_step(stmt_);
      // 新しい行ビューは同じ共有キャッシュを参照する
      current_ = sqlite3_row_view{stmt_, shared_names_};
      return *this;
    }
    sqlite3_row_view const& operator*() const { return current_; }
    bool operator!=(sentinel) const { return rc_ == SQLITE_ROW; }
  };

  iterator begin() const {
    if (!started_) {
      started_ = true;
      auto* names = col_names_ptr();
      int rc = sqlite3_step(stmt_);
      return iterator{stmt_, rc, sqlite3_row_view{stmt_, names}, names};
    }
    return iterator{nullptr, SQLITE_DONE, sqlite3_row_view{nullptr}, nullptr};
  }
  sentinel end() const { return {}; }
};

} // namespace injamm::sqlite3
