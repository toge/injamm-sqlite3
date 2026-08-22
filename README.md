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

テスト済み injamm リビジョン: `9a6d95c` (拡張は `injamm::detail::` の concept 2種と `bc_compile` / `bc_execute(_into)` に依存するため、本体更新時は要確認)

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
| `{{loop.size}}` | ❌ | 事前カウントなし |
| `{{#if age > 18}}` | ❌ | 値は文字列、数値比較不可 |
| 整数フィルタ (`hex`, `zerofill` 等) | ❌ | 文字列→整数変換なし |
| 入れ子パス `{{addr.city}}` | ❌ | 実行時型はリフレクション不可能 |

## 設計

### アーキテクチャ

```
injamm (別リポジトリ、無修正)
  └── bytecode_exec.hpp   runtime dispatch 分岐 (concept 2種) を内包

injamm-sqlite3
  ├── concept.hpp        injamm::detail の concept 再エクスポート
  ├── executor.hpp       bytecode_exec.hpp への名前空間エイリアスシム
  ├── engine.hpp         runtime_engine<T> (bc_compile / bc_execute を利用)
  └── adapter.hpp        sqlite3_row_view, sqlite3_result
```

### 名前空間

- 全て `injamm::sqlite3` 以下
- 内部実装は `injamm::sqlite3::detail` (`injamm::detail` のエイリアス)

### ディスパッチ

GCC では core と共通の computed-goto (threaded) dispatch が自動有効になります(`INJAMM_NO_THREADED_DISPATCH` を自分で定義すれば無効化可能)。`tests/bench_dispatch.cpp` に計測用ベンチマークがあります。
