# FMEngineTest

**FmEngineApi** に準拠した FM 音源エンジンの汎用テストツールです。  
エンジン DLL を実行時に動的ロードするため、YMEngine 以外の実装でも `-e` オプションで切り替えて動作します。

## ファイル構成

```
FMEngineTest/
├── CMakeLists.txt
├── extern/
│   ├── nlohmann_json/   ← git submodule (nlohmann/json)
│   └── rtaudio/         ← git submodule (thestk/rtaudio)
├── src/
│   ├── main.cpp         ← テストアプリ本体
│   └── patches/         ← チップ別テスト用パッチ定義 (JSON)
│       ├── all.json     ← $ref で各チップ JSON を参照
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

## 使い方

```
FMEngineTest [-e <dllpath>] [-r <rate>] [-d <device>] [file.json ...]
```

| オプション | 説明 |
|---|---|
| `-e <dllpath>` | エンジン DLL のパスを指定。省略時は `FmEngineApi.dll` / `libFmEngineApi.so` |
| `-r <rate>` | サンプルレート (Hz)。省略時 48000 |
| `-d <name>` | デバイス名を部分一致で指定。省略時はデフォルトデバイス |
| `file.json` | パッチファイル。複数指定可。省略時は `patches/all.json` |

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
```

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

## JSON パッチフォーマット

```json
{
  "sample_rate": 48000,
  "global": { "note_ms": 800, "rest_ms": 200 },
  "chips": {
    "OPNA": {
      "gain_l": 0.9, "gain_r": 1.0,
      "init": [ {"reg": "0x29", "val": "0x80"} ],
      "channels": [
        {
          "ch": 0, "port": 0,
          "_comment": "CH1 262Hz pan=L",
          "note_ms": 800,
          "init":    [ {"reg": "0xA4", "val": "0x11", "port": 0},
                       {"reg": "0xA0", "val": "0xD5", "port": 0} ],
          "key_on":  [ {"reg": "0x28", "val": "0xF0", "port": 0} ],
          "key_off": [ {"reg": "0x28", "val": "0x00", "port": 0} ]
        }
      ]
    },
    "OPM": { "$ref": "opm.json" }
  }
}
```

### フィールド説明

| フィールド | 説明 |
|---|---|
| `sample_rate` | エンジンのサンプルレート (Hz) |
| `global.note_ms` | デフォルト発音時間 (ms) |
| `global.rest_ms` | デフォルト休符時間 (ms) |
| `chips.<name>` | チップ名は `FmEngine_GetSupportedChip` で列挙される文字列と一致すること |
| `gain` | L/R 共通ゲイン (省略時 1.0) |
| `gain_l` / `gain_r` | 左右独立ゲイン (`gain` より優先) |
| `init` | 初期化レジスタ列 (チップ起動時に1回だけ書き込む) |
| `channels[].ch` | チャンネル番号 (表示用) |
| `channels[].port` | デフォルトポート番号 |
| `channels[].note_ms` | 発音時間 (チャンネル個別設定、省略時 `global.note_ms`) |
| `channels[].rest_ms` | 休符時間 (チャンネル個別設定、省略時 `global.rest_ms`) |
| `channels[].init` | チャンネル初期化レジスタ列 |
| `channels[].key_on` | KEY ON レジスタ列 |
| `channels[].key_off` | KEY OFF レジスタ列 |
| `"$ref": "file.json"` | 同名チップの定義を参照先ファイルから読み込む |

---

## FmEngineApi インターフェース仕様

`FMEngineTest` が使用する C API です。  
この仕様に準拠した DLL であれば、`-e` オプションで切り替えてテストできます。

### エクスポート属性

```c
// Windows
#define FMENGINE_API  __declspec(dllexport)  // またはdllimport
#define FMENGINE_CALL __cdecl

// Linux/macOS
#define FMENGINE_API  __attribute__((visibility("default")))
#define FMENGINE_CALL
```

### 型定義

```c
typedef struct FmEngineOpaque* FmEngineHandle;

typedef enum {
    FM_OK                =  0,
    FM_ERR_INVALID_ARG   = -1,
    FM_ERR_UNKNOWN_CHIP  = -2,  // FmEngine_AddChip で未知のチップ名
    FM_ERR_ALLOC         = -3,
    FM_ERR_UNAVAILABLE   = -4,
} FmResult;

typedef enum {
    FM_MEM_ADPCM_A = 1,  // ADPCM-A ROM (OPNA/OPNB/OPNBB)
    FM_MEM_ADPCM_B = 2,  // ADPCM-B RAM/ROM
    FM_MEM_PCM     = 3,  // PCM ROM (OPL4)
} FmMemoryType;
```

### エンジン生成・破棄

```c
FmEngineHandle FmEngine_Create(uint32_t sample_rate);
void           FmEngine_Destroy(FmEngineHandle engine);
```

### 対応チップ問い合わせ

チップはキーワード文字列で識別します。ヘッダに enum 定数は不要です。

```c
// 対応チップの総数
uint32_t    FmEngine_Inquiry(FmEngineHandle engine);

// index 番目のチップ名。範囲外は nullptr
const char* FmEngine_GetSupportedChip(FmEngineHandle engine, uint32_t index);
```

```c
// 使用例
uint32_t n = FmEngine_Inquiry(eng);
for (uint32_t i = 0; i < n; ++i)
    printf("%s\n", FmEngine_GetSupportedChip(eng, i));
```

### チップ追加

```c
// name : "OPNA", "OPL2" 等 (大文字小文字を区別する)
// clock: マスタークロック Hz。0 で標準クロック
// 戻り値: FM_OK / FM_ERR_UNKNOWN_CHIP / FM_ERR_ALLOC
FmResult FmEngine_AddChip(
    FmEngineHandle engine,
    const char*    name,
    uint32_t       clock,
    uint32_t*      out_id);
```

### チップ情報

```c
const char* FmEngine_GetChipName(FmEngineHandle engine, uint32_t chip_id);
uint32_t    FmEngine_GetNativeRate(FmEngineHandle engine, uint32_t chip_id);
uint32_t    FmEngine_GetSampleRate(FmEngineHandle engine);
```

### レジスタ書き込み

```c
// スレッドセーフ: オーディオコールバックスレッドと並行して呼び出し可能
FmResult FmEngine_Write(
    FmEngineHandle engine,
    uint32_t       chip_id,
    uint8_t        reg,
    uint8_t        value,
    uint32_t       port);   // OPL3/OPNA 等の bank/port 番号
```

### ゲイン設定

```c
// 1.0 = 0 dB。L/R 独立指定
FmResult FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float gain_l, float gain_r);
FmResult FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float* out_gain_l, float* out_gain_r);
```

### 外部メモリ設定

```c
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// data の寿命は呼び出し元が管理すること
FmResult FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size);
uint32_t FmEngine_GetMemorySize(
    FmEngineHandle engine, uint32_t chip_id, FmMemoryType mem_type);
```

### 波形生成

```c
// out_l / out_r : float32 非インターリーブ、範囲 [-1.0, 1.0]
// オーディオコールバックから呼び出すこと
FmResult FmEngine_Generate(
    FmEngineHandle engine,
    float*   out_l,
    float*   out_r,
    uint32_t samples);
```

### エクスポートシンボル一覧

DLL がエクスポートすべきシンボル:

```
FmEngine_Create
FmEngine_Destroy
FmEngine_Inquiry
FmEngine_GetSupportedChip
FmEngine_AddChip
FmEngine_GetChipName
FmEngine_GetNativeRate
FmEngine_GetSampleRate
FmEngine_Write
FmEngine_SetGain
FmEngine_GetGain
FmEngine_SetMemory
FmEngine_GetMemorySize
FmEngine_Generate
```

### C# (P/Invoke) サンプル

```csharp
using System.Runtime.InteropServices;

static class FmEngineApi {
    const string DLL = "FmEngineApi";

    [DllImport(DLL)] public static extern IntPtr  FmEngine_Create(uint sampleRate);
    [DllImport(DLL)] public static extern void    FmEngine_Destroy(IntPtr engine);
    [DllImport(DLL)] public static extern uint    FmEngine_Inquiry(IntPtr engine);
    [DllImport(DLL)] public static extern IntPtr  FmEngine_GetSupportedChip(IntPtr engine, uint index);
    [DllImport(DLL)] public static extern int     FmEngine_AddChip(
        IntPtr engine, string name, uint clock, out uint chipId);
    [DllImport(DLL)] public static extern IntPtr  FmEngine_GetChipName(IntPtr engine, uint chipId);
    [DllImport(DLL)] public static extern uint    FmEngine_GetNativeRate(IntPtr engine, uint chipId);
    [DllImport(DLL)] public static extern uint    FmEngine_GetSampleRate(IntPtr engine);
    [DllImport(DLL)] public static extern int     FmEngine_Write(
        IntPtr engine, uint chipId, byte reg, byte value, uint port);
    [DllImport(DLL)] public static extern int     FmEngine_SetGain(
        IntPtr engine, uint chipId, float gainL, float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_GetGain(
        IntPtr engine, uint chipId, out float gainL, out float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_SetMemory(
        IntPtr engine, uint chipId, int memType, byte[] data, uint size);
    [DllImport(DLL)] public static extern uint    FmEngine_GetMemorySize(
        IntPtr engine, uint chipId, int memType);
    [DllImport(DLL)] public static extern int     FmEngine_Generate(
        IntPtr engine, IntPtr outL, IntPtr outR, uint samples);
}
```

## ライセンス

- **このツール**: MIT
- **RtAudio**: MIT (Gary P. Scavone)
- **nlohmann/json**: MIT (nlohmann)
