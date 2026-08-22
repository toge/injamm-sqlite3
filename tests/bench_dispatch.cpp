// INJAMM_NO_THREADED_DISPATCH の有無によるディスパッチ性能差を測るための計測用バイナリ。
// ループ + 変数 + 文字列フィルタ + 文字列比較を含むテンプレートで sqlite3_result を反復レンダリングし、ns/row を表示する。
// ctest 登録なし。手動実行用: ./build/tests/injamm-sqlite3_bench

#include <chrono>
#include <cstdio>
#include <string>

#include <sqlite3.h>

#include <injamm/sqlite3/adapter.hpp>
#include <injamm/sqlite3/engine.hpp>

int main() {
  constexpr int kRows = 1000;
  constexpr int kIterations = 200;

  sqlite3* db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    std::puts("open failed");
    return 1;
  }
  char* err = nullptr;
  sqlite3_exec(db, "CREATE TABLE t(name TEXT, email TEXT, status TEXT);", nullptr, nullptr, &err);
  sqlite3_exec(db, "BEGIN", nullptr, nullptr, &err);
  {
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO t VALUES(?,?,?)", -1, &ins, nullptr);
    std::string name;
    std::string email;
    for (int i = 0; i < kRows; ++i) {
      name = "user" + std::to_string(i % 100);
      email = "user" + std::to_string(i) + "@example.com";
      const char* status = (i % 2 == 0) ? "active" : "inactive";
      sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 2, email.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 3, status, -1, SQLITE_STATIC);
      sqlite3_step(ins);
      sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
  }
  sqlite3_exec(db, "COMMIT", nullptr, nullptr, &err);

  // ループ({{#.}}) + 変数 + フィルタ(upper) + 文字列比較(if ==) + loop.index1 を含むテンプレート
  auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(
      "<ul>{{#.}}<li>{{name | upper}} <{{email}}>"
      "{{#if status == \"active\"}}*{{/if}}{{loop.index1}}</li>{{/.}}</ul>");

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT name,email,status FROM t", -1, &stmt, nullptr);

  long long checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int it = 0; it < kIterations; ++it) {
    // sqlite3_result は単一パスのため、毎回 reset して新しい result オブジェクトで反復する
    sqlite3_reset(stmt);
    auto html = eng.render(injamm::sqlite3::sqlite3_result{stmt});
    if (!html) {
      std::puts("render failed");
      return 1;
    }
    checksum += static_cast<long long>(html->size());
  }
  const auto end = std::chrono::steady_clock::now();

  const double ms = std::chrono::duration<double, std::milli>(end - start).count();
  const double ns_per_row = ms * 1e6 / (static_cast<double>(kRows) * kIterations);
  std::printf("rows=%d iterations=%d total=%.2fms %.0f ns/row checksum=%lld\n",
              kRows, kIterations, ms, ns_per_row, checksum);

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
