#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <glaze/glaze.hpp>
#include <injamm/detail/nttp_data.hpp>
#include <injamm/engine.hpp>
#include <injamm/sqlite3/adapter.hpp>
#include <injamm/sqlite3/engine.hpp>

#include <sqlite3.h>
#include <string>
#include <vector>

// ---- 共通型 ----

struct BenchRow {
  std::string name;
  std::string email;
  int age{};
  std::string status;
};

template <>
struct glz::meta<BenchRow> {
  static constexpr auto value = glz::object("name", &BenchRow::name, "email", &BenchRow::email, "age", &BenchRow::age,
                                            "status", &BenchRow::status);
};

struct BenchTable {
  std::vector<BenchRow> users;
};

template <>
struct glz::meta<BenchTable> {
  static constexpr auto value = glz::object("users", &BenchTable::users);
};

// ---- ヘルパ ----

static sqlite3* make_db(int nrows) {
  sqlite3* db = nullptr;
  sqlite3_open(":memory:", &db);
  sqlite3_exec(db, "CREATE TABLE users(name TEXT, email TEXT, age INTEGER, status TEXT)", nullptr, nullptr, nullptr);
  sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
  sqlite3_stmt* ins = nullptr;
  sqlite3_prepare_v2(db, "INSERT INTO users VALUES(?,?,?,?)", -1, &ins, nullptr);
  for (int i = 0; i < nrows; ++i) {
    std::string name = "user" + std::to_string(i % 100);
    std::string email = "user" + std::to_string(i) + "@example.com";
    int age = 20 + (i % 50);
    const char* status = (i % 2 == 0) ? "active" : "inactive";
    sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 3, age);
    sqlite3_bind_text(ins, 4, status, -1, SQLITE_STATIC);
    sqlite3_step(ins);
    sqlite3_reset(ins);
  }
  sqlite3_finalize(ins);
  sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
  return db;
}

static std::string col_text(sqlite3_stmt* s, int col) {
  auto* t = sqlite3_column_text(s, col);
  int n = sqlite3_column_bytes(s, col);
  if (!t) return "";
  return std::string(reinterpret_cast<const char*>(t), static_cast<std::size_t>(n));
}

// ---- テンプレート定数 ----
// HTML テーブル（基本ループ + エスケープ）
static constexpr std::string_view kHtmlContainer = "<table>{{#users}}<tr><td>{{name}}</td><td>{{email}}</td><td>{{age}}</td></tr>{{/users}}</table>";
static constexpr std::string_view kHtmlDirect = "<table>{{#.}}<tr><td>{{name}}</td><td>{{email}}</td><td>{{age}}</td></tr>{{/.}}</table>";

// CSV（文字列連結主体）
static constexpr std::string_view kCsvContainer = "name,email,age\n{{#users}}{{name}},{{email}},{{age}}\n{{/users}}";
static constexpr std::string_view kCsvDirect = "name,email,age\n{{#.}}{{name}},{{email}},{{age}}\n{{/.}}";

// フィルタ + 条件分岐 + loop.index1（高負荷）
static constexpr std::string_view kFilterContainer =
    "<ul>{{#users}}<li>{{name | upper}} <{{email}}>{{#if status == \"active\"}}*{{/if}}{{loop.index1}}</li>{{/users}}</ul>";
static constexpr std::string_view kFilterDirect =
    "<ul>{{#.}}<li>{{name | upper}} <{{email}}>{{#if status == \"active\"}}*{{/if}}{{loop.index1}}</li>{{/.}}</ul>";

// 単一行
static constexpr std::string_view kSingleTmpl = "{{name}} <{{email}}> ({{age}}) [{{status}}]";

// ---- 正確性検証 ----

TEST_CASE("sqlite vs container - output equality", "[sqlite][correctness]") {
  // HTML 10 行
  SECTION("html") {
    sqlite3* db = make_db(10);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);

    auto eng_c = injamm::engine<BenchTable>(kHtmlContainer);
    auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kHtmlDirect);

    // container 側を取得してレンダリング
    BenchTable tbl;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      BenchRow r{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)};
      tbl.users.push_back(std::move(r));
    }
    auto out_c = eng_c.render(tbl);
    REQUIRE(out_c.has_value());

    sqlite3_reset(stmt);
    auto out_d = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    REQUIRE(out_d.has_value());
    CHECK(*out_c == *out_d);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }

  // CSV
  SECTION("csv") {
    sqlite3* db = make_db(10);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
    auto eng_c = injamm::engine<BenchTable>(kCsvContainer);
    auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kCsvDirect);

    BenchTable tbl;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      BenchRow r{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)};
      tbl.users.push_back(std::move(r));
    }
    auto out_c = eng_c.render(tbl);
    REQUIRE(out_c.has_value());
    sqlite3_reset(stmt);
    auto out_d = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    REQUIRE(out_d.has_value());
    CHECK(*out_c == *out_d);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }

  // フィルタ + 条件分岐
  SECTION("filter") {
    sqlite3* db = make_db(10);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
    auto eng_c = injamm::engine<BenchTable>(kFilterContainer);
    auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kFilterDirect);

    BenchTable tbl;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      BenchRow r{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)};
      tbl.users.push_back(std::move(r));
    }
    auto out_c = eng_c.render(tbl);
    REQUIRE(out_c.has_value());
    sqlite3_reset(stmt);
    auto out_d = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    REQUIRE(out_d.has_value());
    CHECK(*out_c == *out_d);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }

  // 単一行 row_view vs struct
  SECTION("single row") {
    sqlite3* db = make_db(5);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users LIMIT 1", -1, &stmt, nullptr);

    auto eng_c = injamm::engine<BenchRow>(kSingleTmpl);
    auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(kSingleTmpl);

    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    BenchRow r{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)};
    auto out_c = eng_c.render(r);
    REQUIRE(out_c.has_value());

    auto row_view = injamm::sqlite3::sqlite3_row_view{stmt};
    auto out_d = eng_d.render(row_view);
    REQUIRE(out_d.has_value());
    CHECK(*out_c == *out_d);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }
}

// ---- ベンチマーク ----
// 各 TEST_CASE は 1 つのパターン×行数に対応。
// BENCHMARK 内では sqlite3_reset + (コンテナなら fetch+render) / (directなら直接 render) を計測する。
// engine の構築は BENCHMARK 外（warm）で一度だけ行う。

TEST_CASE("benchmark html 10 rows", "[benchmark]") {
  sqlite3* db = make_db(10);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);

  auto eng_c = injamm::engine<BenchTable>(kHtmlContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kHtmlDirect);

  BENCHMARK("container html 10") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(10);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct html 10") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark html 100 rows", "[benchmark]") {
  sqlite3* db = make_db(100);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);

  auto eng_c = injamm::engine<BenchTable>(kHtmlContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kHtmlDirect);

  BENCHMARK("container html 100") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(100);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct html 100") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark html 1000 rows", "[benchmark]") {
  sqlite3* db = make_db(1000);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);

  auto eng_c = injamm::engine<BenchTable>(kHtmlContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kHtmlDirect);

  BENCHMARK("container html 1000") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(1000);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct html 1000") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark csv 10 rows", "[benchmark]") {
  sqlite3* db = make_db(10);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kCsvContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kCsvDirect);

  BENCHMARK("container csv 10") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(10);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct csv 10") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark csv 100 rows", "[benchmark]") {
  sqlite3* db = make_db(100);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kCsvContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kCsvDirect);

  BENCHMARK("container csv 100") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(100);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct csv 100") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark csv 1000 rows", "[benchmark]") {
  sqlite3* db = make_db(1000);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kCsvContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kCsvDirect);

  BENCHMARK("container csv 1000") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(1000);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct csv 1000") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark filter 10 rows", "[benchmark]") {
  sqlite3* db = make_db(10);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kFilterContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kFilterDirect);

  BENCHMARK("container filter 10") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(10);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct filter 10") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark filter 100 rows", "[benchmark]") {
  sqlite3* db = make_db(100);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kFilterContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kFilterDirect);

  BENCHMARK("container filter 100") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(100);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct filter 100") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark filter 1000 rows", "[benchmark]") {
  sqlite3* db = make_db(1000);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users", -1, &stmt, nullptr);
  auto eng_c = injamm::engine<BenchTable>(kFilterContainer);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(kFilterDirect);

  BENCHMARK("container filter 1000") {
    sqlite3_reset(stmt);
    BenchTable tbl;
    tbl.users.reserve(1000);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      tbl.users.push_back(BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)});
    }
    auto r = eng_c.render(tbl);
    return r ? r->size() : 0;
  };

  BENCHMARK("direct filter 1000") {
    sqlite3_reset(stmt);
    auto r = eng_d.render(injamm::sqlite3::sqlite3_result{stmt});
    return r ? r->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

TEST_CASE("benchmark single row", "[benchmark]") {
  sqlite3* db = make_db(1);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,age,status FROM users LIMIT 1", -1, &stmt, nullptr);

  auto eng_c = injamm::engine<BenchRow>(kSingleTmpl);
  auto eng_d = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(kSingleTmpl);

  BENCHMARK("container single row") {
    sqlite3_reset(stmt);
    BenchRow r{};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      r = BenchRow{col_text(stmt, 0), col_text(stmt, 1), sqlite3_column_int(stmt, 2), col_text(stmt, 3)};
    }
    auto res = eng_c.render(r);
    return res ? res->size() : 0;
  };

  BENCHMARK("direct single row") {
    sqlite3_reset(stmt);
    // sqlite3_row_view は呼び出し側で step 済みである必要がある
    sqlite3_step(stmt);
    auto view = injamm::sqlite3::sqlite3_row_view{stmt};
    auto res = eng_d.render(view);
    return res ? res->size() : 0;
  };

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}
