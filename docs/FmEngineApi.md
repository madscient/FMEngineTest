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

// チップから見えるメモリ。詳細は「外部メモリの割り当て」を参照
typedef enum {
    FM_MEM_ADPCM_A         = 1,  // ADPCM-A (OPNA/OPNB/OPNBB)
    FM_MEM_ADPCM_B         = 2,  // ADPCM-B (OPNA/OPNB/OPNBB/Y8950)。OPNA/Y8950 では RAM モードのメモリ
    FM_MEM_PCM             = 3,  // PCM (OPL4)
    FM_MEM_ADPCM_B_ROMMODE = 4,  // ADPCM-B の ROM モードのメモリ (OPNA/Y8950)。FmEngine_SetMemoryEx 専用
} FmMemoryType;

// 外部メモリにつないだデバイスの種類。詳細は「外部メモリの割り当て」を参照
typedef enum {
    FM_ACCESS_ROM = 0,
    FM_ACCESS_RAM = 1,
} FmMemoryAccess;

// 出力の部位。チップが別々の端子から出す出力を表す。
// 番号はチップをまたいで重ならない。詳細は「部位ごとのゲイン」を参照
typedef enum {
    FM_PART_OPN_FM      = 0,  // OPN/OPNA/OPNB/OPNBB: FM 部 (ADPCM・リズムを含む)
    FM_PART_OPN_SSG     = 1,  //   SSG 部
    FM_PART_OPLL_MELODY = 2,  // OPLL/OPLLP/OPLLX/VRC7: メロディ
    FM_PART_OPLL_RHYTHM = 3,  //   リズム
    FM_PART_OPL3_AB     = 4,  // OPL3: 出力 A (L) / B (R)
    FM_PART_OPL3_CD     = 5,  //   出力 C (L) / D (R)
    FM_PART_OPL4_DO0    = 6,  // OPL4: DO0 (FM の C/D)
    FM_PART_OPL4_DO1    = 7,  //   DO1 (AWM の C/D)
    FM_PART_OPL4_DO2    = 8,  //   DO2 (FM の A/B と AWM の A/B のミックス)
} FmPart;
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

// ネイティブサンプルレート (Hz、端数切り捨て)
// FM と SSG を別のレートで生成するチップ (OPN 系) では FM 部のレート
// OPN/OPNA では prescale レジスタ (0x2D-0x2F) の書き込みで変わる
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
// オーディオコールバックスレッドと並行して呼び出し可能
FmResult FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float gain_l, float gain_r);
FmResult FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float* out_gain_l, float* out_gain_r);
```

## 部位ごとのゲイン (任意)

この節の関数は任意のエクスポートです。エクスポートしていない DLL もこの仕様に準拠します。  
呼び出し側は `GetProcAddress` / `dlsym` でシンボルの有無を確かめてから呼び出してください。シンボルが無いエンジンでは、どのチップも部位を持たないものとして扱います。

```c
// 実際に掛かるゲインは FmEngine_SetGain のゲイン × 部位のゲイン
// チップが持たない部位や未知の chip_id を指定すると FM_ERR_INVALID_ARG
// オーディオコールバックスレッドと並行して呼び出し可能
FmResult FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, FmPart part,
    float gain_l, float gain_r);
FmResult FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, FmPart part,
    float* out_gain_l, float* out_gain_r);

// チップが持つ部位のビットマスク (bit n = FmPart の n 番)
// 部位を持たないチップは 0。未知の chip_id は FM_ERR_INVALID_ARG
FmResult FmEngine_GetPartMask(
    FmEngineHandle engine, uint32_t chip_id, uint32_t* out_mask);
```

| 部位 | 対象チップ | 内容 | 既定値 |
|---|---|---|---|
| `FM_PART_OPN_FM`      | OPN, OPNA, OPNB, OPNBB | FM 部 (ADPCM・リズムを含む) | 1.0 |
| `FM_PART_OPN_SSG`     | OPN, OPNA, OPNB, OPNBB | SSG 部 | 1.0 |
| `FM_PART_OPLL_MELODY` | OPLL, OPLLP, OPLLX, VRC7 | メロディ | 1.0 |
| `FM_PART_OPLL_RHYTHM` | OPLL, OPLLP, OPLLX, VRC7 | リズム | 1.0 |
| `FM_PART_OPL3_AB`     | OPL3 | 出力 A (L) / B (R) | 1.0 |
| `FM_PART_OPL3_CD`     | OPL3 | 出力 C (L) / D (R) | 0 |
| `FM_PART_OPL4_DO0`    | OPL4 | DO0 (FM の C/D) | 0 |
| `FM_PART_OPL4_DO1`    | OPL4 | DO1 (AWM の C/D) | 0 |
| `FM_PART_OPL4_DO2`    | OPL4 | DO2 (FM の A/B と AWM の A/B のミックス) | 1.0 |

- 表に無いチップ (OPL, OPL2, Y8950, OPN2, OPM, OPZ など出力が1系統のもの) は部位を持ちません。ゲインは `FmEngine_SetGain` で設定します。
- C/D 側 (`FM_PART_OPL3_CD`, `FM_PART_OPL4_DO0`, `FM_PART_OPL4_DO1`) の既定値が 0 なのは、FM の出力先を A/B/C/D 全部にしたチャンネルが A/B と C/D に同じ音を出し、混ぜると二重に足されるためです。
- 部位の番号はこの仕様書で割り当てます。新しい部位には表の末尾に続く番号を割り当て、既存の番号は変えません。マスクが `uint32_t` なので、番号は 0〜31 の範囲です。

```c
// 使用例
typedef FmResult (*PFN_FmEngine_GetPartMask)(FmEngineHandle, uint32_t, uint32_t*);
PFN_FmEngine_GetPartMask getPartMask =
    (PFN_FmEngine_GetPartMask)GetProcAddress(dll, "FmEngine_GetPartMask");

uint32_t mask = 0;
if (getPartMask && getPartMask(eng, opna_id, &mask) == FM_OK
    && (mask & (1u << FM_PART_OPN_SSG))) {
    // SSG のゲインを設定できる
}
```

## 外部メモリ設定

```c
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// data の寿命は呼び出し元が管理すること
// mem_type に FM_MEM_ADPCM_B_ROMMODE は使わない
FmResult FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size);
uint32_t FmEngine_GetMemorySize(
    FmEngineHandle engine, uint32_t chip_id, FmMemoryType mem_type);
```

ここでいう「外部メモリ」は**音源コアの外部**という意味で、必ずしも物理的にチップの外側に存在することを意味しません。

`FmEngine_SetMemory` の `data` は書き込み可能なメモリであるとは限りません。エンジンは `data` に書き込みません。
エンジンがデータを複製するか参照するか、複製した場合にチップからの書き込みを複製に反映するかは、エンジンのコア実装によります。
ROM と RAM を区別して割り当てるには [`FmEngine_SetMemoryEx`](#外部メモリの割り当て-任意) を使います。

## 外部メモリの割り当て (任意)

この節の関数は任意のエクスポートです。エクスポートしていない DLL もこの仕様に準拠します。  
呼び出し側は `GetProcAddress` / `dlsym` でシンボルの有無を確かめてから呼び出してください。シンボルが無いエンジンでは `FmEngine_SetMemory` だけが使えます。  
エクスポートするかどうかの判断基準は[節の末尾](#エクスポートするかどうかの判断基準)を参照してください。

```c
// mem_type のメモリの [base, base + size) に data を割り当てる
// data == NULL ならその範囲と重なる割り当てをすべて外す (access は無視)
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// 戻り値:
//   FM_ERR_INVALID_ARG : 未知の chip_id、チップが持たない mem_type、size が 0、
//                        既存の割り当てと範囲が重なる
//   FM_ERR_UNAVAILABLE : FM_ACCESS_RAM のブロックをその場で読み書きできない
FmResult FmEngine_SetMemoryEx(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, uint32_t base,
    uint8_t* data, uint32_t size, FmMemoryAccess access);
```

| `mem_type` | 対象チップ | 内容 |
|---|---|---|
| `FM_MEM_ADPCM_A`         | OPNA | リズム音の内蔵 ROM の内容 |
| `FM_MEM_ADPCM_A`         | OPNB, OPNBB | ADPCM-A のメモリ |
| `FM_MEM_ADPCM_B`         | OPNA, Y8950 | ADPCM-B の ROM/RAM 選択ビットが RAM のときにアクセスするメモリ |
| `FM_MEM_ADPCM_B`         | OPNB, OPNBB | ADPCM-B のメモリ |
| `FM_MEM_ADPCM_B_ROMMODE` | OPNA, Y8950 | ADPCM-B の ROM/RAM 選択ビットが ROM のときにアクセスするメモリ |
| `FM_MEM_PCM`             | OPL4 | PCM のメモリ |

OPNA と Y8950 の ROM/RAM 選択ビットはアクセスの方法を切り替えるもので、ROM モードと RAM モードでは別のメモリにアクセスします。ROM モードのメモリに RAM をつなぐこともできるので、選択ビットはメモリが書き込めるかどうかを表しません。

`access` は、つないだデバイスの種類を表します。

| | `FM_ACCESS_ROM` | `FM_ACCESS_RAM` |
|---|---|---|
| ブロックの内容 | 割り当て中は変わらない | チップ以外が書き換えてもよい |
| エンジンによる複製 | してよい | しない。ブロックをその場で読み書きする |
| チップが書き込んだとき | 捨てる | ブロックに書く |

チップがいつメモリに書き込むか (ROM モード中は書き込まない、など) はチップの動作によるもので、`access` では決まりません。

- 番地 `base + i` のバイトは `data[i]` に対応します。バイトの中身の並び (チップが読むビットの順序、実機のメモリの配置との対応など) はこの API では定めません。アプリケーションとエンジンの間で取り決めてください。
- 割り当ての無い番地を読むと 0 です。割り当ての無い番地への書き込みは捨てます。
- 割り当てを外すか `FmEngine_Destroy` が戻るまで、`data` を解放しないでください。エンジンは `data` を解放しません。

`FM_ACCESS_RAM` のブロックについて、エンジンは次を守ります。

1. ブロックに触るのは `FmEngine_SetMemoryEx`・`FmEngine_Write`・`FmEngine_Generate` の実行中だけ
2. 呼び出し側が `FmEngine_Write` の前にブロックへ書いた値は、その `FmEngine_Write` がチップに反映される時点でチップから見える
3. `FmEngine_Write` によってチップがメモリに書いた値は、その `FmEngine_Write` が戻った後に始まった `FmEngine_Generate` が戻った時点でブロックに入っている

これ以外の並行アクセスは保証しません。ブロックを別のデバイスと共有する場合、`FmEngine_Generate` の実行中に別のスレッドからブロックに触らないようにするのは呼び出し側の責任です (エンジンを止める、など)。

### エクスポートするかどうかの判断基準

外部メモリのバスが外に出ているチップを扱うエンジンは、この関数をエクスポートしておくことをお勧めします。外部回路によって、そのメモリをホストや他のデバイスと共有する構成を取りうるためです。  
メモリがチップ内で完結していて、外にバスが出ていない場合 (OPNA のリズム用メモリなど) は、この限りではありません。

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

DLL がエクスポートしなければならないシンボル (必須):

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

DLL がエクスポートしてもよいシンボル (任意。[部位ごとのゲイン](#部位ごとのゲイン-任意)):

```
FmEngine_SetPartGain
FmEngine_GetPartGain
FmEngine_GetPartMask
```

DLL がエクスポートしてもよいシンボル (任意。[外部メモリの割り当て](#外部メモリの割り当て-任意)):

```
FmEngine_SetMemoryEx
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
    // 任意シンボル。DLL がエクスポートしているときだけ呼ぶこと
    [DllImport(DLL)] public static extern int     FmEngine_SetPartGain(
        IntPtr engine, uint chipId, int part, float gainL, float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_GetPartGain(
        IntPtr engine, uint chipId, int part, out float gainL, out float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_GetPartMask(
        IntPtr engine, uint chipId, out uint mask);
    [DllImport(DLL)] public static extern int     FmEngine_SetMemory(
        IntPtr engine, uint chipId, int memType, byte[] data, uint size);
    [DllImport(DLL)] public static extern uint    FmEngine_GetMemorySize(
        IntPtr engine, uint chipId, int memType);
    // 任意シンボル。DLL がエクスポートしているときだけ呼ぶこと
    // data は呼び出しの後もエンジンが使うので、GC が動かさないメモリ
    // (Marshal.AllocHGlobal、固定した GCHandle など) を渡すこと
    [DllImport(DLL)] public static extern int     FmEngine_SetMemoryEx(
        IntPtr engine, uint chipId, int memType, uint baseAddr,
        IntPtr data, uint size, int access);
    [DllImport(DLL)] public static extern int     FmEngine_Generate(
        IntPtr engine, IntPtr outL, IntPtr outR, uint samples);
}
```
