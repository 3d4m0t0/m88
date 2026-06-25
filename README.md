# M88 Linux 版 (Qt)

cisc 氏作の Windows 向け PC-8801 エミュレータ [M88](http://retropc.net/cisc/m88/) を、[rururutan/m88](https://github.com/rururutan/m88) から派生させ、Linux (x86_64) 向け Qt 6 フロントエンド **`m88-qt`** として移植したものです。

**版 1.2.1** — [更新履歴](#更新履歴)

### English

**M88 for Linux (Qt)**

A Linux (x86_64) port of cisc’s Windows PC-8801 emulator [M88](http://retropc.net/cisc/m88/), derived from [rururutan/m88](https://github.com/rururutan/m88), with a Qt 6 frontend **`m88-qt`**.

**Version 1.2.1** — see [Changelog](#更新履歴) (更新履歴).

## AI の利用について

本プロジェクトでは AI 支援 IDE [Cursor](https://cursor.com/) を利用し、生成されたコードやデザインパターンを必要に応じて取り入れています。採用した生成物は、いずれも制作者がレビュー・修正・統合しています。

紹介プログラム経由の登録用リンク（**紹介リンク**）: [cursor.com/referral?code=TI3UQLE9PFH3](https://cursor.com/referral?code=TI3UQLE9PFH3)  
このリンクから登録すると Cursor 側の紹介特典が適用される場合がありますが、M88 の開発・配布とは無関係です。

### English

This project uses the AI-assisted IDE [Cursor](https://cursor.com/). Generated code and design patterns are incorporated where helpful; the maintainer reviews, revises, and integrates all adopted material.

Referral registration link: [cursor.com/referral?code=TI3UQLE9PFH3](https://cursor.com/referral?code=TI3UQLE9PFH3). Cursor's referral program may apply at sign-up; this is unrelated to the development or distribution of M88.

## 系譜

系譜は次のとおりです。

1. [cisc 氏作 M88](http://retropc.net/cisc/m88/)（Windows 向け、`readme.txt`）
2. cisc 氏版を改造した [rururutan/m88](https://github.com/rururutan/m88)（Windows 向け、`README_WINDOWS.md`）
3. 上記をフォークし、Linux (x86_64) に移植した本版（`m88-qt`）

Linux 版でメンテナンス対象としているのは **Qt 版のみ** です。SDL2 版 (`m88`) は試作段階で未実装・未解決の部分が多く、CMake でもデフォルトではビルドしません。

## 更新履歴

### 1.2.1 (`1.2.1`)

* **Qt 設定 UI** — タブをハードウェア / 映像・音声に再構成。タブごとの標準設定ボタン、各種レイアウト調整
* **設定の保存** — FM ボード ($44h / $A8h) の選択が正しく保存・復元されるよう修正
* **i18n** — 全ロケールを en/ja に同期。`translations/sync_i18n.py` を追加

### English

* **Qt settings UI** — reorganized tabs into Hardware and Audio/Video; per-tab standard settings and layout updates
* **Config persistence** — FM board ($44h / $A8h) choices now save and restore correctly
* **i18n** — all locales synced to en/ja; added `translations/sync_i18n.py`

### 1.2.0 (`1.2.0`)

* **エミュレーションコア** — Z80 デュアル CPU の同期・WAIT・I/O タイミングを Windows 版 (Z80_x86) に合わせて再現。FDIF / ディスク読込時のハング修正
* **安定性** — VM Pause の割込みペーシング、メニュー操作時のデッドロック回避。Debug ビルド向け exec ストールウォッチドッグ
* **画面・リセット** — モード切替・ジョイパッド挙動の修正。リセット後の暗転 (V2) 修正、Windows 版と同等のリセット経路
* **音声 (OPNIF)** — TimeEvent の Update/Count 順序を上流に復元
* **Qt UI** — 最近使ったディスクイメージメニュー (QSettings)。設定からレジスタウォッチを削除
* **パッケージング / i18n** — ランチャー metadata の UTF-8 化。日本語 UI の表記修正

### English

* **Emulation core** — Z80 dual-CPU sync, WAIT, and I/O timing aligned with the Windows build (Z80_x86); FDIF / disk-read hang fixes
* **Stability** — VM pause interrupt pacing and menu deadlock avoidance; exec stall watchdog in Debug builds
* **Display / reset** — mode-switch and joypad behavior fixes; V2 dark screen after reset; reset path matched to Windows
* **Sound (OPNIF)** — restore upstream TimeEvent Update/Count order
* **Qt UI** — recent disk image menu (QSettings); register watch removed from settings
* **Packaging / i18n** — UTF-8 launcher metadata; Japanese UI wording fixes

## 仕様

Windows 版（rururutan/m88）からの主な差分は次のとおりです。

* 機能の追加・削除を行っています
* **UI 多言語化 (i18n)** — en / ja / zh-CN / zh-TW / ko / de / fr / es（en と ja はバイナリ埋め込み、他は `share/m88-qt/translations/` の JSON。`M88_LANG` またはシステムロケールで選択）
* **マルチディスクイメージの編集**（Disk → Edit multi-disk image...）
* **IME 経由での半角カナ入力**（fcitx-mozc 等でかなを入力して確定すると、半角カタカナが入力できます）
* rururutan/m88 版にあった C86CTL 関連は非対応です
* Tape および Debug 関連も現時点では非対応です

### English

Main differences from the Windows build (rururutan/m88):

* Features have been added and removed compared to the Windows port.
* **UI localization (i18n)** — en / ja / zh-CN / zh-TW / ko / de / fr / es (en and ja embedded in the binary; others load JSON from `share/m88-qt/translations/`; select via `M88_LANG` or system locale)
* **Multi-disk image editor** (Disk → Edit multi-disk image...)
* **Half-width kana input via IME** (with fcitx-mozc and similar IMEs, confirming kana input produces half-width katakana)
* C86CTL support from rururutan/m88 is not supported.
* Tape and debug features are not supported at this time.

## 動作環境

* Linux (x86_64)
* Qt 6 ランタイム (Widgets)
* X11 または Wayland 上のデスクトップセッション
* 音声出力可能な環境（miniaudio 経由; PulseAudio / PipeWire / ALSA 等）
* `pc88.rom` または `n88.rom` 等の ROM データ（所有する PC-8801 本体から吸い出したもの）

### テスト環境

主に以下で `m88-qt` のビルド・動作確認を行っています。

* **OS:** openSUSE Tumbleweed (x86_64)
* **セッション:** Wayland
* **Qt:** 6.11 系 (Widgets)
* **音声:** PipeWire / PulseAudio 互換、ALSA（miniaudio; バックエンド auto / pulse / alsa）
* **IME:** fcitx-mozc（半角カナ入力）

他のディストリビューションでも動作する想定ですが、上記以外では十分に検証していません。

### English

**Test environment**

Build and runtime checks are mainly performed on:

* **OS:** openSUSE Tumbleweed (x86_64)
* **Session:** Wayland
* **Qt:** 6.11.x (Widgets)
* **Audio:** PipeWire / PulseAudio-compatible stack, ALSA (miniaudio; backends auto / pulse / alsa)
* **IME:** fcitx-mozc (half-width kana input)

Other distributions may work, but are not extensively tested by the maintainer.

## 依存ライブラリ

ビルド時:

* C/C++ コンパイラ (GCC または Clang、C++17 対応)
* CMake 3.20 以上
* Qt 6 (Widgets モジュール)
* pkg-config (推奨)

実行時:

* Qt 6 ランタイム (Widgets)
* 標準 C ライブラリ (`pthread`, `dl`, `m` 等)

音声出力は同梱の [miniaudio](https://github.com/mackron/miniaudio) (`third_party/miniaudio/`) を使用します。ビルド時に ALSA / PulseAudio / SDL2 などを追加でリンクする必要はありません。

各ディストリビューションの開発パッケージ例:

* Debian / Ubuntu: `build-essential`, `cmake`, `ninja-build`, `pkg-config`, `qt6-base-dev`
* Fedora: `gcc-c++`, `cmake`, `ninja-build`, `pkgconf-pkg-config`, `qt6-qtbase-devel`
* openSUSE: `gcc-c++`, `cmake`, `ninja`, `pkg-config`, `qt6-gui-devel`

## ビルド

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

生成物は `build/m88-qt` です。Release がデフォルトです。

openSUSE などで `CXX=ccache g++` が設定されている環境でも CMake が自動で補正します。以前の失敗で作った `build/` がある場合は削除してから再実行してください（`ccache: invalid option -- 't'` や AutoMoc エラー）。

| CMake オプション | デフォルト | 説明 |
|---|---|---|
| `M88_BUILD_QT_FRONTEND` | ON | Qt 6 版 `m88-qt` |
| `M88_BUILD_LINUX_FRONTEND` | OFF | SDL2 版 `m88` (未完成) |
| `M88_NATIVE_ARCH` | OFF | GCC/Clang で `-march=native` |

## インストール

**システム全体へ（例）**
```bash
sudo cmake --install build --prefix /usr
```

**ユーザーローカルへ（例）** — `~/.local/bin` を PATH に含めること
```bash
cmake --install build --prefix "$HOME/.local"
```

**パッケージ用ステージング（例）** — RPM / deb 等
```bash
DESTDIR=/tmp/m88-root cmake --install build --prefix /usr
```

`--prefix /usr` では `bin/m88-qt`、desktop、AppStream、icons、man、`share/doc/m88-qt/`、`share/m88-qt/translations/`（en/ja 以外の UI 翻訳）等を配置。UI 言語は `M88_LANG` またはシステムロケール（`M88_TRANSLATIONS_DIR` で翻訳ディレクトリを上書き可）。

### English

After building, run **one** of: `sudo cmake --install build --prefix /usr` (system), `cmake --install build --prefix "$HOME/.local"` (user-local; add `~/.local/bin` to `PATH`), or `DESTDIR=... cmake --install build --prefix /usr` (package staging). English/Japanese UI is embedded; other locales load JSON from `share/m88-qt/translations/`.

## ディレクトリ構成

```
m88/
├── CMakeLists.txt          # Linux ビルド定義
├── cmake/M88Install.cmake  # install 規則
├── data/                   # desktop / AppStream / man / icons
├── translations/           # 外部 UI 翻訳 JSON
├── README.md               # 本ドキュメント (Linux / Qt)
├── README_WINDOWS.md       # rururutan/m88 版 (cisc 氏オリジナルは readme.txt)
├── src/
│   ├── pc88/               # PC-8801 エミュレーションコア
│   ├── devices/            # Z80, OPN/OPNA 等
│   ├── common/             # 共通ユーティリティ
│   ├── linux/              # Linux 共通 (設定, 描画, キーボード, スレッド等)
│   ├── linux_compat/       # Win32 API 互換 (ファイル I/O, パス, ROM ログ等)
│   ├── qt/                 # Qt6 フロントエンド (m88-qt)
│   └── win32/              # Windows 版 (Linux では WinKeyIF 等一部のみリンク)
├── third_party/
│   └── miniaudio/          # 音声出力 (ヘッダ同梱)
└── build/                  # ビルド出力 (任意)
```

Windows 向け Visual Studio プロジェクト (`M88_2008.sln`, `src/win32/`) もリポジトリに含まれますが、Linux ビルドでは使用しません。

## データレイアウト

起動時に **2 種類** の配置が自動選択されます (`src/linux/linux_paths.cpp`)。

### 1. 作業ディレクトリモード

カレントディレクトリに `m88.ini` (または `M88.ini`) がある場合、設定とデータはそのディレクトリ基準です。

```
./
├── m88.ini
├── m88_keyfix.ini      (キー修正; 必要に応じて生成)
├── roms/               ROM / リズム WAV
├── snapshot/           スナップショット (.s88)
└── (画面キャプチャ .bmp / 録音 .wav もここ)
```

### 2. ユーザ設定モード

作業ディレクトリに ini が無い場合、`~/.config/m88/` を作成して使用します。

```
~/.config/m88/
├── m88.ini
├── m88_keyfix.ini
├── roms/
└── snapshot/
```

画面キャプチャ (.bmp) とサウンド録音 (.wav) の自動保存先は **ホームディレクトリ** (`$HOME/`) です。

いずれのモードでも `--config PATH` で ini を明示指定できます。`--rom-dir PATH` を指定すると ROM / WAV の読み込み先を上書きします (未指定時は上記 `roms/`)。

起動ログに `data layout:` / `rom directory:` 等が stderr に出力されます。

## ROM ファイル

ROM は **所有する実機から吸い出したもの** を使用してください（下記「ライセンス」参照）。

読み込みディレクトリ (`roms/` または `--rom-dir`):

| ファイル | 必須 | 用途 |
|---|---|---|
| `pc88.rom` | ※ | 一体型 ROM (32K+32K+32K バンドル) |
| `n88.rom` | ※ | 分割 ROM 時の N88 基本 ROM (32K) |
| `n80.rom` | 任意 | N80 基本 ROM (32K) |
| `n88_0.rom` … `n88_3.rom` | 任意 | 拡張 ROM (各 8K) |
| `disk.rom` | 任意 | サブ CPU ディスク ROM (8K); 無い場合は内蔵スタブ |
| `kanji1.rom`, `kanji2.rom` | 任意 | 漢字 ROM |
| `font.rom` | 任意 | テキストフォント; 無ければ `kanji1.rom` から代替 |
| `font80sr.rom` | 任意 | N80SR ひらがな CG フォント |
| `jisyo.rom`, `cdbios.rom`, `n80_2.rom`, `n80_3.rom`, `e1.rom`…`e8.rom` | 任意 | 拡張カード用 |

※ `pc88.rom` または `n88.rom` のどちらか一方が必須です。`pc88.rom` があれば分割 ROM は不要です。

ファイル名は Linux では **小文字** を想定しています (`pc88.rom`, `disk.rom` 等)。

## WAV ファイル

### リズム音源 (読み込み)

YM2608 (OPNA) のリズムサンプル。ROM と同じディレクトリ (`roms/` 等) に置きます。

* `2608_BD.WAV`, `2608_SD.WAV`, `2608_TOP.WAV`, `2608_HH.WAV`, `2608_TOM.WAV`, `2608_RIM.WAV`
* 最後の RIM が無い場合は `2608_RYM.WAV` を試行
* 拡張子 `.WAV` / `.wav` はどちらでも可

無い場合は該当リズム音なしで動作します (起動時に読み込みログが stdout に出ます)。

## 実行例

```bash
./build/m88-qt --rom-dir ~/.config/m88/roms -d0 game.d88
./build/m88-qt --scale 2
./build/m88-qt --config ./m88.ini
```

## ライセンス

### 系譜とドキュメント

| 版 | 説明 | ドキュメント |
|---|---|---|
| cisc 氏作 M88 | オリジナル | `readme.txt` |
| [rururutan/m88](https://github.com/rururutan/m88) | cisc 氏版の改造（Windows） | `README_WINDOWS.md` |
| 本リポジトリ | rururutan/m88 を Linux に移植（`m88-qt`） | `README.md`（本ファイル） |

ファイルによって適用されるライセンスが異なります。

### Linux ポートで新規追加したコード

`src/linux/`, `src/linux_compat/`, `src/qt/` および Linux 向け CMake 等、本ポートで新規に追加したファイルは **2 条項 BSD ライセンス** です（`README_WINDOWS.md` 参照）。

### rururutan/m88 から引き継いだコード

`README_WINDOWS.md` に記載のとおり、rururutan/m88 で新規追加されたファイルも 2 条項 BSD ライセンスです。

### 既存の M88 ソースコード（cisc 氏）

`src/pc88/`, `src/devices/`, `src/common/`, `src/win32/` 等、オリジナル M88 から引き継いだファイルは **cisc 氏のオリジナルライセンス** に従います（`readme.txt` / `README_WINDOWS.md`「ライセンス」節）。

* M88 は cisc 氏が著作権を所有しています。
* M88 とそのソースコードは一切無保証です。
* M88 そのもの、または M88 の使用や、M88 を使用できなかったことなど、M88 に関して生じた損害はすべて使用者が自ら負うものとします。作者は一切責任を負いません。また、作者は M88 に関してバグ、不具合等があったとしてもそれに対処する義務を負いません。
* M88 の転載、及び配布は禁止します。但し，M88 のソースコードに改変を加えたもの，及び M88 のソースコードを利用したソフトに関しては，その限りではありません。
* M88.exe の使用者は NEC PC-8801 シリーズの本体を所有しなければなりません。また、使用する ROM データはその本体から直接取り出した ROM データでなければなりません。使用者の所有物でない本体から取り出した ROM データは使用できません。
* M88 のソースコードの一部，または全部を組み込んだソフトは，フリーソフトとして公開することが出来ます。
* 但し，`src/pc88/` のディレクトリの下にあるファイルを組み込む場合，または商用ソフト(シェアウェア含む)へのプラグインソフトとして配布する場合は，同時にそのソフトのソースコードもフリーソフトとして公開ください。
* 公開の際には，ドキュメント等に M88 のソースコードの一部または全部を組み込んだ事と，著作権表示を明示してください．また，作者への連絡を頂ければ幸いです。
* M88 のソースコードを利用したソフトのソースを配布する際には，M88 のソースコードのうち，そのソフトのコンパイルに必要なものに限り，添付することを認めます。
* 商用ソフト(シェアウェア含む) に M88 のソースコードの一部，または全部を組み込む際には，事前に M88 の作者の合意を得る必要があります。
* M88 に改変を加えたソフトを配布する場合は，M88 の著作権表示，および改変内容を明示してください。

Linux 版 (`m88-qt`) を使用する場合も、上記 ROM に関する条件（実機所有・自己吸出し ROM のみ使用）は **同等に適用** されます。

### 第三者ライブラリ

* **zlib** (`src/zlib/`) — zlib License（Jean-loup Gailly / Mark Adler）
* **miniaudio** (`third_party/miniaudio/`) — パブリックドメインまたは MIT No Attribution（miniaudio 同梱ヘッダの表記に従う）

各ライブラリの利用・再配布は、それぞれのライセンス条件に従ってください。
