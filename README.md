# FMEngineTest

**FmEngineApi** に準拠した FM 音源エンジンの汎用テストツールです。  
エンジン DLL を実行時に動的ロードするため、YMEngine 以外の実装でも `-e` オプションで切り替えて動作します。

## ファイル構成

```
FMEngineTest/
├── CMakeLists.txt
├── docs/
│   ├── FmEngineApi.md       ← FmEngineApi C インターフェース仕様
│   └── patch-format.md      ← パッチ / テストスイート JSON フォーマット仕様
├── extern/
│   ├── nlohmann_json/        ← git submodule (nlohmann/json)
│   └── rtaudio/              ← git submodule (thestk/rtaudio)
├── scripts/
│   ├── collect_engines.bat   ← 互換エンジン DLL を収集するスクリプト (Windows)
│   └── collect_engines.sh    ← 互換エンジン DLL を収集するスクリプト (Linux/macOS)
├── src/
│   ├── main.cpp              ← テストアプリ本体
│   └── patches/              ← チップ別テスト用パッチ定義 (JSON)
│       ├── all.json          ← $ref で各チップ JSON を参照
│       ├── test_suite_example.json  ← テストスイートの記述例
│       ├── opna.json
│       └── ...
└── README.md
```

## セットアップ

```bash
git clone https://github.com/your-org/FMEngineTest
cd FMEngineTest
git submodule update --init --recursive
```

## ビルド

FmEngineApi DLL はビルド時にリンクしません。実行時に動的ロードします。

### Windows (Visual Studio 2022)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

成果物: `build/bin/FMEngineTest` (または `FMEngineTest.exe`)

## エンジン DLL の準備

[FmEngineApi 互換エミュレータ](#fmengineapi互換エミュレータ) をビルドまたはダウンロードし、  
実行ファイルと同じディレクトリに配置してください。

`scripts/collect_engines.bat` (Windows) または `scripts/collect_engines.sh` (Linux/macOS) を使うと、  
複数の互換エンジンを一括クローン・ビルドして配置できます。詳しくは [スクリプトの使い方](#スクリプトの使い方) を参照してください。

## 使い方

```
FMEngineTest [-e <dllpath>] [-r <rate>] [-d <device>] [file.json ...]
```

| オプション | 説明 |
|---|---|
| `-e <dllpath>` | エンジン DLL のパスを指定。省略時は `FmEngineApi.dll` / `libFmEngineApi.so` |
| `-r <rate>` | サンプルレート (Hz)。省略時 48000 |
| `-d <name>` | デバイス名を部分一致で指定。省略時はデフォルトデバイス |
| `file.json` | パッチファイルまたはテストスイートファイル。複数指定可。省略時は `patches/all.json` |

### 実行例

```cmd
# デフォルトエンジン、全チップテスト
FMEngineTest

# 別エンジンを指定
FMEngineTest -e NukedEngine.dll patches/opm.json

# サンプルレートとデバイスを指定
FMEngineTest -e FmEngineApi.dll -r 44100 -d "Realtek" patches/opna.json

# 複数パッチファイルを順番にテスト
FMEngineTest patches/opl2.json patches/opna.json

# テストスイートで複数エンジン・レートを横断テスト
FMEngineTest patches/test_suite_example.json

# WAV エクスポート (テストスイートで output_wav を指定)
FMEngineTest my_export_suite.json
```

テストスイートおよびパッチ JSON の書き方は [`docs/patch-format.md`](docs/patch-format.md) を参照してください。

### 起動時の出力例

```
FMEngineTest

Loading engine: FmEngineApi.dll
Engine loaded.

Sample rate: 48000 Hz

Supported chips (16): Y8950 OPL OPL2 OPL3 OPL4 OPN OPNA OPNB OPNBB OPN2 OPM OPLL OPLLP OPLLX OPZ VRC7

Audio devices:
  [1] Speakers (Realtek Audio) (default)
  [2] HDMI Output

Using device id=1

Opening stream (device=1, rate=48000, frames=512)...
Stream started (bufferFrames=512)

[OPL2] chip_id=0, native_rate=49716 Hz
  CH0 261Hz ...
```

### ROM ファイル (ADPCM)

ADPCM チップ (OPNA/OPNB/OPNBB) は ROM ファイルが必要です。  
実行ファイルと同じフォルダに配置してください。  
存在しない場合は ADPCM チャンネルが無音になるだけです。

| ファイル名 | 対象チップ |
|---|---|
| `ym2608.rom` | OPNA |
| `ym2610.rom` | OPNB, OPNBB |
| `ym2610b.rom` | OPNB, OPNBB |

ROM ファイルの入手はエンドユーザーの責任で行ってください。

## スクリプトの使い方

`scripts/collect_engines.bat` (Windows) / `scripts/collect_engines.sh` (Linux/macOS) は、  
互換エンジンリポジトリを `engines/` にクローンしてビルドし、  
DLL/SO を `build/bin/` にコピーします。

```cmd
# Windows — ビルド先ディレクトリを指定して実行
scripts\collect_engines.bat build\bin\Release

# Linux / macOS
bash scripts/collect_engines.sh build/bin
```

スクリプト内の `ENGINES` リストを編集することで、ビルド対象を増減できます。

## FmEngineApi互換エミュレータ

- [madscient/YMEngine](https://github.com/madscient/YMEngine)
- [madscient/NukedEngine](https://github.com/madscient/NukedEngine)
- [madscient/FMgenEngine](https://github.com/madscient/FMgenEngine)
- [madscient/DSAemuEngine](https://github.com/madscient/DSAemuEngine)
- [madscient/DBOPLEngine](https://github.com/madscient/DBOPLEngine)
- [madscient/SAASoundEngine](https://github.com/madscient/SAASoundEngine)
- [madscient/SCCIBridgeEngine](https://github.com/madscient/SCCIBridgeEngine)

## ライセンス

- **このツール**: MIT
- **RtAudio**: MIT (Gary P. Scavone)
- **nlohmann/json**: MIT (nlohmann)
