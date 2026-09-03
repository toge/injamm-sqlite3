// WASI スモークテスト: wasip1 + sqlite3 amalgamation 下で主要機能を検証する
// (Catch2 は wasip1 でビルド不可のため非依存。親 injamm の test_wasi_minimal と同様)
#include <injamm/sqlite3/adapter.hpp>
#include <injamm/sqlite3/engine.hpp>
#include <sqlite3.h>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

sqlite3_stmt* prepare(sqlite3* db, const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    check(false, "prepare");
    return nullptr;
  }
  return stmt;
}

} // namespace

int main() {
  sqlite3* db = nullptr;
  check(sqlite3_open(":memory:", &db) == SQLITE_OK, "open :memory:");
  check(sqlite3_exec(db, "CREATE TABLE users (id INTEGER, name TEXT, email TEXT)", nullptr, nullptr, nullptr) ==
            SQLITE_OK,
        "create table");
  check(sqlite3_exec(db, "INSERT INTO users VALUES (1, 'Alice', 'alice@example.com')", nullptr, nullptr,
                     nullptr) == SQLITE_OK,
        "insert Alice");
  check(sqlite3_exec(db, "INSERT INTO users VALUES (2, 'Bob', 'bob@example.com')", nullptr, nullptr, nullptr) ==
            SQLITE_OK,
        "insert Bob");

  // 単一行レンダリング
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT name, email FROM users WHERE id = 1");
    check(sqlite3_step(stmt) == SQLITE_ROW, "step single row");
    auto row = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{name}} <{{email}}>");
    auto result = eng.render(row);
    check(result.has_value(), "single row has_value");
    if (result) check(*result == "Alice <alice@example.com>", "single row value");
    sqlite3_finalize(stmt);
  }

  // 複数行ループ
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT name FROM users ORDER BY id");
    auto result_set = injamm::sqlite3::sqlite3_result{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}{{name}} {{/.}}");
    auto result = eng.render(result_set);
    check(result.has_value(), "multi row has_value");
    if (result) check(*result == "Alice Bob ", "multi row value");
    sqlite3_finalize(stmt);
  }

  // 整数列の文字列化
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT id, name FROM users WHERE id = 2");
    check(sqlite3_step(stmt) == SQLITE_ROW, "step integer row");
    auto row = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{id}}:{{name}}");
    auto result = eng.render(row);
    check(result.has_value(), "integer has_value");
    if (result) check(*result == "2:Bob", "integer value");
    sqlite3_finalize(stmt);
  }

  // NULL 列は空文字
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT NULL AS val");
    check(sqlite3_step(stmt) == SQLITE_ROW, "step null row");
    auto row = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("[{{val}}]");
    auto result = eng.render(row);
    check(result.has_value(), "null has_value");
    if (result) check(*result == "[]", "null value");
    sqlite3_finalize(stmt);
  }

  // 文字列フィルタ + 数値比較
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT name, id AS age FROM users ORDER BY id");
    auto result_set = injamm::sqlite3::sqlite3_result{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(
        "{{#.}}{{name | upper}}{{#if age > 1}}*{{/if}};{{/.}}");
    auto result = eng.render(result_set);
    check(result.has_value(), "filter has_value");
    if (result) check(*result == "ALICE;BOB*;", "filter value");
    sqlite3_finalize(stmt);
  }

  // 空結果セット + else
  {
    sqlite3_stmt* stmt = prepare(db, "SELECT name FROM users WHERE id = 999");
    auto result_set = injamm::sqlite3::sqlite3_result{stmt};
    auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}body{{else}}empty{{/.}}");
    auto result = eng.render(result_set);
    check(result.has_value(), "empty has_value");
    if (result) check(*result == "empty", "empty value");
    sqlite3_finalize(stmt);
  }

  sqlite3_close(db);

  if (failures == 0) std::printf("ALL OK\n");
  return failures == 0 ? 0 : 1;
}
