# injamm-sqlite3

injamm のテンプレートエンジンをベースに、**sqlite3_stmt から直接 HTML をレンダリング** するためのヘッダオンリー C++23 ライブラリ。

## コンセプト

通常の injamm は glaze リフレクションを用いて C++ 構造体からテンプレートに値を渡す。  
injamm-sqlite3 は **中間の構造体を経由せず**、SQL の結果セットを直接テンプレートに流し込む。

```cpp
// 従来: SQL → struct → template
struct user { std::string name; std::string email; };
auto eng = injamm::engine<user>("{{name}} <{{email}}>");
eng.render(user{name, email});

// injamm-sqlite3: SQL → 直接レンダリング
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(
  "{{name}} <{{email}}>"
);
eng.render(sqlite3_row_view{stmt});
```

## 要件

- C++23 コンパイラ (GCC 14+ 推奨)
- [injamm](https://github.com/toge/injamm) 本体 (`find_package(injamm CONFIG REQUIRED)` で解決。事前にビルド&インストールが必要)
- unofficial-sqlite3 (vcpkg port、テスト実行時は加えてシステムの libsqlite3)

テスト済み injamm リビジョン: `0c459ea` (拡張は `injamm::detail::` の concept 2種と `bc_compile` / `bc_execute(_into)` に依存するため、本体更新時は要確認)

## ビルド

```sh
# 1) 先に本体をビルド & インストール
cmake -B /tmp/injamm-b -S ~/src/injamm \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local/injamm \
  -DBUILD_TEST=OFF -DBUILD_EXAMPLE=OFF
cmake --build /tmp/injamm-b --parallel
cmake --install /tmp/injamm-b

# 2) 本ライブラリ
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=$HOME/.local/injamm
cmake --build build --parallel
ctest --test-dir build
```

CMake オプション: `BUILD_TEST`(デフォルト ON)。

## WASI (wasip1) ビルド

vcpkg の sqlite3 ポートは wasip1 でビルド失敗する
(os_unix の `UNIX_SHM_*` が未定義になる) ため、WASI に限って
sqlite3 アマルガメーション (`sqlite3.c`) を単一ファイルコンパイルする。
Catch2 も wasip1 でビルド不可 (signal 未対応) のため、
WASI テストは Catch2 非依存のスモークテスト (`tests/test_wasi_sqlite3.cpp`) になる。
enchantum は injamm 本体と同様に WASI でも有効のまま使う。

```sh
# 1) injamm 本体を wasip1 向けにビルド & インストール
cmake -B /tmp/injamm-wasi-b -S ~/src/injamm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=wasm32-wasip1 \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/triplets \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=~/vm/wasi-sdk/share/cmake/wasi-sdk-p1.cmake \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local/injamm-wasi \
  -DBUILD_TEST=OFF -DBUILD_EXAMPLE=OFF \
  -DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"
cmake --build /tmp/injamm-wasi-b
cmake --install /tmp/injamm-wasi-b

# 2) 本ライブラリ (sqlite アマルガメーションは自動取得)
cmake -B build-wasi -S . -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=wasm32-wasip1 \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/triplets \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=~/vm/wasi-sdk/share/cmake/wasi-sdk-p1.cmake \
  -DCMAKE_PREFIX_PATH=$HOME/.local/injamm-wasi \
  -Dinjamm_DIR=$HOME/.local/injamm-wasi/lib/cmake/injamm \
  -DINJAMM_SQLITE3_FETCH_AMALGAMATION=ON
cmake --build build-wasi
ctest --test-dir build-wasi -V   # wasmedge 経由で .wasm を実行
```

wasip2 の場合も同様ですが、`wasip1` を `wasip2` に、`wasi-sdk-p1.cmake` を `wasi-sdk-p2.cmake` に置き換えてください。

CMake オプション: `INJAMM_SQLITE3_FETCH_AMALGAMATION`(sqlite アマルガメーションを
FetchContent で自動取得、デフォルト OFF)、`SQLITE3_AMALGAMATION_DIR`
(手元のアマルガメーション展開ディレクトリを指定)、
`SQLITE3_AMALGAMATION_VERSION` / `SQLITE3_AMALGAMATION_SHA256`
(取得するバージョンとハッシュ、デフォルト 3.53.4)。

## 使い方

### 単一行

```cpp
#include <injamm/sqlite3/engine.hpp>
#include <injamm/sqlite3/adapter.hpp>

sqlite3_stmt* stmt;
sqlite3_prepare_v2(db, "SELECT name, email FROM users WHERE id = ?", -1, &stmt, nullptr);
sqlite3_bind_int(stmt, 1, 42);
sqlite3_step(stmt);

auto row = injamm::sqlite3::sqlite3_row_view{stmt};
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_row_view>(
  "{{name}} <{{email}}>"
);
auto result = eng.render(row);
// result → expected<string>  ("Alice <alice@example.com>")
```

### 複数行 ({{#.}})

```cpp
sqlite3_stmt* stmt;
sqlite3_prepare_v2(db, "SELECT name FROM users", -1, &stmt, nullptr);

auto rows = injamm::sqlite3::sqlite3_result{stmt};
auto eng = injamm::sqlite3::runtime_engine<injamm::sqlite3::sqlite3_result>(
  "<ul>{{#.}}<li>{{name}}</li>{{/.}}</ul>"
);
auto html = eng.render(rows);
// → "<ul><li>Alice</li><li>Bob</li></ul>"
```

## 制限

| 機能 | 状態 | 理由 |
|---|---|---|
| `{{var}}` / `{{{var}}}` | ✅ | エスケープ / 生出力 |
| 文字列フィルタ (`upper`, `trim` 等) | ✅ | 値は文字列として扱われる |
| `{{#.}}...{{/.}}` (行ループ) | ✅ | 前方専用カーソル |
| `{{#if var}}` | ✅ | 非空文字列は真 |
| `{{^var}}` (反転セクション) | ✅ | 空文字列は偽 |
| `{{#if status == "Pending"}}` / `!=` | ✅ | 文字列リテラルとの等値 / 不等値比較 |
| `{{loop.index}}` / `{{loop.index1}}` | ✅ | カウンタ |
| `{{loop.is_first}}` | ✅ | `index == 0` |
| `{{loop.is_last}}` | ❌ | 前方カーソルでは判定不可 |
| `{{loop.is_even}}` / `{{loop.is_odd}}` | ✅ | `index % 2` による偶奇判定（`{{#loop.is_even}}` / `{{^loop.is_odd}}` 等も対応、2026-08-22 追加） |
| `{{loop.size}}` | ❌ | 事前カウントなし |
| `{{#if age > 18}}` | ✅ | 整数として解釈できる文字列は数値比較（本体側で対応） |
| 整数フィルタ (`hex`, `zerofill` 等) | ✅ | 実行時に文字列→整数変換して適用 |
| 文字列フィルタ `urlencode` / `strip` 等 | ✅ | 本体の `filters.hpp` に委譲（`urlencode` は 2026-08-22 追加） |
| セクションフィルタ `join` / `sort` | ❌ | 前方カーソルでは `size()` を持たないためストリーミング不可。`join` は `string::join` 等で代替、`sort` は 2026-08-29 に本体側で削除され事前ソートに移行 |
| 入れ子パス `{{addr.city}}` | ❌ | 実行時型はリフレクション不可能 |

## 設計

### アーキテクチャ

```
injamm (別リポジトリ)
  └── bytecode_exec.hpp   runtime dispatch 分岐 (concept 2種) を内包、
                          実行時文字列値の数値比較 ({{#if age > 18}})・
                          loop.is_even/is_odd・urlencode 等のフィルタに対応

injamm-sqlite3
  ├── concept.hpp        injamm::detail の concept 再エクスポート
  ├── executor.hpp       bytecode_exec.hpp への名前空間エイリアスシム
  ├── engine.hpp         runtime_engine<T> (bc_compile / bc_execute を利用、
  │                      ConstMap / partials コンストラクタを含む)
  └── adapter.hpp        sqlite3_row_view, sqlite3_result (string_view ゼロコピー)
```

### 名前空間

- 全て `injamm::sqlite3` 以下
- 内部実装は `injamm::sqlite3::detail` (`injamm::detail` のエイリアス)

### ディスパッチ

GCC では core と共通の computed-goto (threaded) dispatch が自動有効になります(`INJAMM_NO_THREADED_DISPATCH` を自分で定義すれば無効化可能)。`tests/bench_dispatch.cpp` に計測用ベンチマークがあります。

## ベンチマーク

`injamm`（コンテナ経由）と `injamm-sqlite3`（直接）の比較を Catch2 の `BENCHMARK` 機能で計測します。`template-benchmark` の HTML/CSV/フィルタ等の複数パターンを参考に、SQLite の結果セットを「一度 `vector` に詰め替えて描画」と「直接描画」で比較します。

### 実行方法

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/vm/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=$HOME/.local/injamm
cmake --build build --parallel
ctest -R bench_vs -V        # または ./build/tests/injamm-sqlite3_bench_vs
# Catch2 オプション: --benchmark-samples 100 --benchmark-warmup-time 100
```

`tests/bench_sqlite_vs.cpp` に 4パターン×3サイズ（10/100/1000行）を実装し、各ケースで `container` vs `direct` のペアを `BENCHMARK` で計測します。出力が一致することは `TEST_CASE("sqlite vs container - output equality")` で検証しています。

- **HTMLテーブル**: `<table>{{#users}}<tr><td>{{name}}</td><td>{{email}}</td><td>{{age}}</td></tr>{{/users}}</table>`
- **CSV**: `name,email,age\n{{#users}}{{name}},{{email}},{{age}}\n{{/users}}`
- **フィルタ+条件分岐+loop.index**: `<ul>{{#users}}<li>{{name | upper}} <{{email}}>{{#if status == "active"}}*{{/if}}{{loop.index1}}</li>{{/users}}</ul>`
- **単一行**: `{{name}} <{{email}}> ({{age}}) [{{status}}]`（`sqlite3_row_view` vs `struct`）

### 結果（Release, GCC 16, x64 Linux, Catch2 100 samples）

最適化前（`find()` が毎回 `sqlite3_column_name` 線形探索 + `std::string` ヒープ確保）と最適化後（列名キャッシュ + `string_view` ゼロコピー）の比較です。

| パターン | 行数 | container mean | direct mean（最適化前） | direct mean（最適化後） | 改善後 direct/container |
|---|---|---|---|---|---|
| html | 10 | 13.5 µs | 13.5 µs | 13.5 µs | 1.00× |
| html | 100 | 119 µs | 157 µs | 118 µs | 0.99× |
| html | 1000 | 1.13 ms | 1.47 ms | 1.31 ms | 1.16× |
| csv | 10 | 14.2 µs | 17.3 µs | 14.2 µs | 1.00× |
| csv | 100 | 112 µs | 148 µs | 121 µs | 1.08× |
| csv | 1000 | 1.28 ms | 1.58 ms | 1.18 ms | 0.92× |
| filter | 10 | 13.2 µs | 18.6 µs | 16.0 µs | 1.21× |
| filter | 100 | 116 µs | 168 µs | 140 µs | 1.20× |
| filter | 1000 | 1.13 ms | 1.68 ms | 1.28 ms | 1.13× |
| single | 1 | 2.67 µs | 3.63 µs | 3.11 µs | 1.16× |

> 最適化により全パターンで 1.3〜1.5倍遅 → 1.1倍以内（多くは±10%）まで改善。`csv 1000` では直接が逆転しています。残る 10〜20% は `find()` の線形探索（4列で4回 `string_view` 比較/行）vs コンテナの `field_index` によるジャンプテーブル差で、テンプレート内の変数名を事前に列インデックスへ解決する `field_index` ヒントの活用でさらに解消可能です（今後の課題）。

### injamm-sqlite3 の有利な点

速度が同等になった現在、直接レンダリングの価値は速度以外にあります。

| 観点 | container（`injamm`） | direct（`injamm-sqlite3`） |
|---|---|---|
| **コード量** | `struct BenchRow` + `glz::meta` + `fetch` ループ（`bench_sqlite_vs.cpp:16,208`）がクエリ毎に必要 | `runtime_engine<sqlite3_result>(kHtmlDirect)` の1行。列名がそのまま `{{name}}` に対応 |
| **メモリ** | `vector<Row>` に全行を保持（1000行で数十KB、並行リクエストで増大） | `sqlite3_result` が1行ずつ `sqlite3_step` し `out.append` するストリーム。同時保持は1行分のみで O(1) |
| **柔軟性** | `SELECT *` など動的列は struct を再定義・再コンパイルが必要 | 任意の列をテンプレート側で `{{extra_col}}` と書くだけで対応。スキーマ変更時に C++ 側の修正が不要 |

> 10行固定の小規模クエリのみで速度だけを見るならコンテナでも十分ですが、100行以上のリスト、動的列、多数のエンドポイントを持つ Web アプリでは、上記3点が開発・運用コストを明確に下げます。ベンチマークは `tests/bench_sqlite_vs.cpp` で再現可能です。
