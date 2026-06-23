# FmEngineApi インターフェース仕様

`FMEngineTest` が使用する C API です。  
この仕様に準拠した DLL であれば、`-e` オプションで切り替えてテストできます。

## エクスポート属性

```c
// Windows
#define FMENGINE_API  __declspec(dllexport)  // またはdllimport
#define FMENGINE_CALL __cdecl

// Linux/macOS
#define FMENGINE_API  __attribute__((visibility("default")))
#define FMENGINE_CALL
```

## 型定義

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

## エンジン生成・破棄

```c
FmEngineHandle FmEngine_Create(uint32_t sample_rate);
void           FmEngine_Destroy(FmEngineHandle engine);
```

## 対応チップ問い合わせ

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

## チップ追加

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

## チップ情報

```c
const char* FmEngine_GetChipName(FmEngineHandle engine, uint32_t chip_id);
uint32_t    FmEngine_GetNativeRate(FmEngineHandle engine, uint32_t chip_id);
uint32_t    FmEngine_GetSampleRate(FmEngineHandle engine);
```

## レジスタ書き込み

```c
// スレッドセーフ: オーディオコールバックスレッドと並行して呼び出し可能
FmResult FmEngine_Write(
    FmEngineHandle engine,
    uint32_t       chip_id,
    uint8_t        reg,
    uint8_t        value,
    uint32_t       port);   // OPL3/OPNA 等の bank/port 番号
```

## ゲイン設定

```c
// 1.0 = 0 dB。L/R 独立指定
FmResult FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float gain_l, float gain_r);
FmResult FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float* out_gain_l, float* out_gain_r);
```

## 外部メモリ設定

```c
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// data の寿命は呼び出し元が管理すること
FmResult FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size);
uint32_t FmEngine_GetMemorySize(
    FmEngineHandle engine, uint32_t chip_id, FmMemoryType mem_type);
```

## 波形生成

```c
// out_l / out_r : float32 非インターリーブ、範囲 [-1.0, 1.0]
// オーディオコールバックから呼び出すこと
FmResult FmEngine_Generate(
    FmEngineHandle engine,
    float*   out_l,
    float*   out_r,
    uint32_t samples);
```

## エクスポートシンボル一覧

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

## C# (P/Invoke) サンプル

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
