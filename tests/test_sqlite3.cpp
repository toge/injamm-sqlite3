#include <catch2/catch_test_macros.hpp>
#include <injamm/sqlite3/adapter.hpp>
#include <injamm/sqlite3/engine.hpp>
#include <sqlite3.h>
#include <string>

struct test_db {
  sqlite3* db = nullptr;
  test_db() {
    sqlite3_open(":memory:", &db);
    sqlite3_exec(db, "CREATE TABLE users (id INTEGER, name TEXT, email TEXT)", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO users VALUES (1, 'Alice', 'alice@example.com')", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT INTO users VALUES (2, 'Bob', 'bob@example.com')", nullptr, nullptr, nullptr);
  }
  ~test_db() { sqlite3_close(db); }
};

sqlite3_stmt* prepare(test_db& db, const char* sql) {
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(db.db, sql, -1, &stmt, nullptr);
  return stmt;
}

TEST_CASE("sqlite3 adapter", "[sqlite3]") {
  // 単一行: 名前とメールをプレースホルダで展開
  SECTION("single row rendering") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT name, email FROM users WHERE id = 1");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{name}} <{{email}}>");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice <alice@example.com>");

    sqlite3_finalize(stmt);
  }

  // 複数行: {{#.}} セクションで各行を展開
  SECTION("multiple rows with {{#.}}") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT name FROM users ORDER BY id");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}{{name}} {{/.}}");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice Bob ");

    sqlite3_finalize(stmt);
  }

  // 整数列: 数値としてそのまま出力される
  SECTION("integer column") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT id, name FROM users WHERE id = 2");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{id}}:{{name}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "2:Bob");

    sqlite3_finalize(stmt);
  }

  // NULL 列: 空文字として出力される
  SECTION("NULL column renders empty") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT NULL AS val");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("[{{val}}]");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "[]");
    sqlite3_finalize(stmt);
  }

  // 非空テキスト列は if セクションで真と判定される
  SECTION("text column truthiness works in if") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT 'Pending' AS status");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if status}}YES{{else}}NO{{/if}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");

    sqlite3_finalize(stmt);
  }

  // テキスト列の等値比較: {{#if status == "Pending"}} が評価される
  SECTION("enum-like text equality works in if") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT 'Pending' AS status");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if status == \"Pending\"}}YES{{else}}NO{{/if}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");

    sqlite3_finalize(stmt);
  }

  // 空の結果セット: セクション内は展開されない
  SECTION("empty result set") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT name FROM users WHERE id = 999");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("before{{#.}}{{name}}{{/.}}after");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "beforeafter");

    sqlite3_finalize(stmt);
  }

  // 空結果セットで else ブロックが描画される
  SECTION("{{#.}} with else, empty") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT name FROM users WHERE id = 999");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}body{{else}}empty{{/.}}");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "empty");

    sqlite3_finalize(stmt);
  }

  // 非空結果セットで本体ブロックが描画される
  SECTION("{{#.}} with else, non-empty") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT name FROM users WHERE id = 1");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}{{name}}{{else}}empty{{/.}}");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice");

    sqlite3_finalize(stmt);
  }
}
