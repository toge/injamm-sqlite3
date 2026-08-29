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

  // 整数列の数値比較: {{#if age > 18}} が評価される
  SECTION("numeric comparison with integer column") {
    test_db db;
    sqlite3_stmt* stmt = prepare(db, "SELECT id AS age FROM users WHERE id = 2");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if age > 1}}GT{{else}}LE{{/if}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "GT");
    sqlite3_finalize(stmt);

    stmt = prepare(db, "SELECT id AS age FROM users WHERE id = 1");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    auto row2   = injamm::sqlite3::sqlite3_row_view{stmt};
    auto result2 = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if age > 1}}GT{{else}}LE{{/if}}").render(row2);
    REQUIRE(result2.has_value());
    CHECK(*result2 == "LE");
    sqlite3_finalize(stmt);
  }

  // 整数フィルタ: 文字列値でも hex / zerofill が適用される
  SECTION("integer filters on runtime values") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT 255 AS v, 7 AS n");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{v | hex}} {{n | zerofill(4)}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "ff 0007");

    sqlite3_finalize(stmt);
  }

  // TEXT 列に保存された数値でも数値比較が行われる（本体側の数値評価対応）
  SECTION("numeric comparison with text-stored value") {
    test_db db;
    sqlite3_exec(db.db, "CREATE TABLE ages (age TEXT)", nullptr, nullptr, nullptr);
    sqlite3_exec(db.db, "INSERT INTO ages VALUES ('20'), ('17')", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt = prepare(db, "SELECT age FROM ages WHERE age = '20'");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if age > 18}}GT{{else}}LE{{/if}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "GT");
    sqlite3_finalize(stmt);

    stmt = prepare(db, "SELECT age FROM ages WHERE age = '17'");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    auto row2    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto result2 = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{#if age > 18}}GT{{else}}LE{{/if}}").render(row2);
    REQUIRE(result2.has_value());
    CHECK(*result2 == "LE");
    sqlite3_finalize(stmt);
  }

  // FLOAT 列: to_chars による小数レンダリング
  SECTION("float column rendering") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT CAST(2.5 AS REAL) AS ratio");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("x={{ratio}}");
    auto result = eng.render(row);
    REQUIRE(result.has_value());
    CHECK(*result == "x=2.5");

    sqlite3_finalize(stmt);
  }

  // render(value, out): out をクリアして書き込む（本体 bc_execute_into の仕様）
  SECTION("render writes into provided string") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT name FROM users WHERE id = 1");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto        row    = injamm::sqlite3::sqlite3_row_view{stmt};
    auto        eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{name}}!");
    std::string out    = "[";
    auto        result = eng.render(row, out);
    REQUIRE(result.has_value());
    CHECK(out == "Alice!");

    sqlite3_finalize(stmt);
  }

  // 反転セクション {{^var}}: 値が空のときだけ描画される
  SECTION("inverted section") {
    test_db       db;
    sqlite3_stmt* stmt = prepare(db, "SELECT name FROM users WHERE id = 1");
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);

    auto row = injamm::sqlite3::sqlite3_row_view{stmt};

    auto eng_empty    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{^missing}}N{{/missing}}");
    auto empty_result = eng_empty.render(row);
    REQUIRE(empty_result.has_value());
    CHECK(*empty_result == "N");

    auto eng_nonempty    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>("{{^name}}N{{/name}}");
    auto nonempty_result = eng_nonempty.render(row);
    REQUIRE(nonempty_result.has_value());
    CHECK(*nonempty_result == "");

    sqlite3_finalize(stmt);
  }

  // loop.is_first: 先頭行でのみ真
  SECTION("loop.is_first") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT id FROM users ORDER BY id");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}{{#if loop.is_first}}F{{/if}}{{/.}}");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "F");

    sqlite3_finalize(stmt);
  }

  // trim_blocks / lstrip_blocks オプション: タグ前後の空白・改行が除去される
  SECTION("trim_blocks and lstrip_blocks options") {
    test_db       db;
    sqlite3_stmt* stmt       = prepare(db, "SELECT name FROM users ORDER BY id");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    // trim_blocks: タグ直後の改行を除去 / lstrip_blocks: ブロックタグ前の行頭空白を除去
    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("A\n  {{#.}}{{name}}{{/.}}\nB", true, true);
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "A\nAliceBobB");

    sqlite3_finalize(stmt);
  }

  // 文字列の不等値比較: status != "Pending"
  SECTION("text inequality works in if") {
    test_db db;
    sqlite3_exec(db.db, "CREATE TABLE orders (status TEXT)", nullptr, nullptr, nullptr);
    sqlite3_exec(db.db, "INSERT INTO orders VALUES ('Pending'), ('Shipped')", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt       = prepare(db, "SELECT status FROM orders");
    auto          result_set = injamm::sqlite3::sqlite3_result{stmt};

    auto eng    = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>("{{#.}}{{#if status != \"Pending\"}}{{status}}{{/if}}{{/.}}");
    auto result = eng.render(result_set);
    REQUIRE(result.has_value());
    CHECK(*result == "Shipped");

    sqlite3_finalize(stmt);
  }
}

