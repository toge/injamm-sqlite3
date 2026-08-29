#include <catch2/catch_test_macros.hpp>
#include <injamm/sqlite3/concept.hpp>
#include <map>
#include <string>

using namespace injamm::sqlite3;

// runtime_field_accessible を満たすモック行（キー→文字列マップ）
struct mock_row {
  std::string find(std::string_view key) const {
    auto it = data_.find(std::string(key));
    return it != data_.end() ? it->second : "";
  }
  std::map<std::string, std::string> data_;
};

// forward_iterable を満たすモック結果セット（行配列のビュー）
struct mock_result {
  mock_row*   rows_;
  std::size_t count_;

  struct iterator {
    mock_row* ptr_;
    mock_row* last_;
    auto&     operator++() {
      ++ptr_;
      return *this;
    }
    auto& operator*() { return *ptr_; }
    bool  operator!=(std::nullptr_t) const { return ptr_ < last_; }
  };

  using value_type = mock_row;
  iterator       begin() const { return iterator{rows_, rows_ + count_}; }
  std::nullptr_t end() const { return nullptr; }
};

TEST_CASE("concepts satisfy", "[concept]") {
  // mock_row が runtime_field_accessible を満たすことを静的検証
  SECTION("mock_row is runtime_field_accessible") {
    static_assert(runtime_field_accessible<mock_row>);
  }
  // mock_result が forward_iterable を満たすことを静的検証
  SECTION("mock_result is forward_iterable") {
    static_assert(forward_iterable<mock_result>);
  }
  // find の正常系・未検出時の挙動を確認
  SECTION("mock_row find works") {
    mock_row r{{{"name", "Alice"}}};
    CHECK(r.find("name") == "Alice");
    CHECK(r.find("nonexistent") == "");
  }
}

#include <injamm/sqlite3/engine.hpp>

#include <cstdio>

TEST_CASE("mock rendering", "[engine]") {
  // 単一行・単一変数の展開
  SECTION("single row, single var") {
    mock_row r{{{"name", "Alice"}, {"email", "alice@example.com"}}};
    fprintf(stderr, "DEBUG: find(name)='%s'\n", r.find("name").c_str());
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("Hello {{name}} ({{email}})");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    fprintf(stderr, "DEBUG: result='%s'\n", result->c_str());
    CHECK(*result == "Hello Alice (alice@example.com)");
  }

  // 未知キーは空文字として出力される
  SECTION("unknown key renders empty") {
    mock_row r{{{"name", "Bob"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("Hi {{name}}, age={{age}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "Hi Bob, age=");
  }

  // エスケープあり/なし（{{{}}}）の出力動作を確認
  SECTION("{{{raw}}} variable output") {
    mock_row r{{{"html", "<b>bold</b>"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{html}} vs {{{html}}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "&lt;b&gt;bold&lt;/b&gt; vs <b>bold</b>");
  }

  // 文字列フィルタ（upper）が適用される
  SECTION("string filter works") {
    mock_row r{{{"name", "alice"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{name | upper}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "ALICE");
  }

  // 非空文字列は if で真と判定される
  SECTION("string truthiness works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }

  // 空文字列は if で偽と判定される
  SECTION("empty string is falsy in if") {
    mock_row r{{{"status", ""}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "NO");
  }

  // 文字列等値比較: {{#if status == "Pending"}} を評価
  SECTION("enum-like string equality works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status == \"Pending\"}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }

  // 文字列不等比較: {{#if status != "Active"}} を評価
  SECTION("enum-like string inequality works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status != \"Active\"}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }
}

TEST_CASE("forward iteration rendering", "[engine][iteration]") {
  // 2 行を {{#.}} セクションで展開
  SECTION("two rows with {{#.}}") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "Alice"}}}, mock_row{{{"name", "Bob"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("<ul>{{#.}}<li>{{name}}</li>{{/.}}</ul>");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "<ul><li>Alice</li><li>Bob</li></ul>");
  }

  // リテラル接頭なしの {{name}} は融合されず展開される
  SECTION("{{name}} without literal prefix (no fusion)") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "Alice"}}}, mock_row{{{"name", "Bob"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{name}} {{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice Bob ");
  }

  // ループ内の loop.index が 0 始まりで出力される
  SECTION("loop.index works") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "A"}}}, mock_row{{{"name", "B"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{loop.index}}:{{name}} {{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "0:A 1:B ");
  }

  // 空結果セットはセクション内が展開されない
  SECTION("empty result renders nothing") {
    mock_result res{nullptr, 0};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("before{{#.}}middle{{/.}}after");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "beforeafter");
  }

  // 空結果セットで else ブロックが描画される
  SECTION("{{#.}} with else, empty") {
    mock_result res{nullptr, 0};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}body{{else}}empty{{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "empty");
  }

  // 非空結果セットで本体ブロックが描画される
  SECTION("{{#.}} with else, non-empty") {
    mock_row    rows_arr[1] = {mock_row{{{"name", "A"}}}};
    mock_result res{rows_arr, 1};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{name}}{{else}}empty{{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "A");
  }

  // loop.is_even / loop.is_odd: 偶奇で交互に true/false (2026-08-22 追加)
  SECTION("loop.is_even and loop.is_odd") {
    mock_row    rows_arr[4] = {mock_row{{{"name", "a"}}}, mock_row{{{"name", "b"}}}, mock_row{{{"name", "c"}}},
                               mock_row{{{"name", "d"}}}};
    mock_result res{rows_arr, 4};
    auto        eng = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{loop.is_even}}|{{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "true|false|true|false|");
    auto eng2 = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{#loop.is_even}}E{{/loop.is_even}}{{^loop.is_even}}O{{/loop.is_even}}|{{/.}}");
    auto r2 = eng2.render(res);
    REQUIRE(r2.has_value());
    CHECK(*r2 == "E|O|E|O|");
  }

  // urlencode フィルタ: スペース等が percent エンコードされる (2026-08-22 追加)
  SECTION("urlencode filter") {
    mock_row r{{{"q", "a b+c"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{q | urlencode}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "a%20b%2Bc");
  }

  // strip は trim の別名
  SECTION("strip filter alias") {
    mock_row r{{{"v", "  hi  "}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{v | strip}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "hi");
  }
}
