# FmEngineApi インターフェース仕様

`FMEngineTest` が使用する C API です。  
この仕様に準拠した DLL であれば、`-e` オプションで切り替えてテストできます。

C の宣言は [`include/FmEngineApi.h`](../include/FmEngineApi.h) にあります。互換エンジンとアプリケーションは、このヘッダの写しを使ってください。

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

// 外部メモリにつないだデバイスの種類。詳細は「外部メモリの割り当て」を参照
typedef enum {
    FM_ACCESS_ROM = 0,
    FM_ACCESS_RAM = 1,
} FmMemoryAccess;
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
// clock: マスタークロック Hz。0 は FM_ERR_INVALID_ARG
// 戻り値: FM_OK / FM_ERR_INVALID_ARG / FM_ERR_UNKNOWN_CHIP / FM_ERR_ALLOC
FmResult FmEngine_AddChip(
    FmEngineHandle engine,
    const char*    name,
    uint32_t       clock,
    uint32_t*      out_id);
```

エンジンは既定のクロックを持ちません。同じチップでも機種によってクロックが異なり、F-Number などのレジスタ値はクロックを前提に計算するため、呼び出し側が必ず指定します。

## チップ情報

```c
const char* FmEngine_GetChipName(FmEngineHandle engine, uint32_t chip_id);

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

チップによっては、音を複数の端子から別々に出します。この出力のひとつひとつを部位と呼びます。  
部位はキーワード文字列で識別します (大文字小文字を区別する)。チップが持つ部位はエンジンに問い合わせて取得するので、ヘッダに定数は不要です。

この節の関数は任意のエクスポートです。エクスポートしていない DLL もこの仕様に準拠します。  
エクスポートするときは、4 つを必ず組にします。呼び出し側は `GetProcAddress` / `dlsym` で `FmEngine_GetPartCount` の有無を確かめ、無ければこの節のどの関数も呼ばず、どのチップも部位を持たないものとして扱ってください。`FmEngine_GetPartCount` を持たずに `FmEngine_SetPartGain` / `FmEngine_GetPartGain` をエクスポートする DLL は、第 3 引数が文字列ではなく、この仕様と互換性がありません。

```c
// チップが持つ部位の数。部位を持たないチップと未知の chip_id は 0
uint32_t    FmEngine_GetPartCount(FmEngineHandle engine, uint32_t chip_id);

// index 番目の部位の名前。範囲外と未知の chip_id は nullptr
// 文字列は FmEngine_Destroy が戻るまで有効
const char* FmEngine_GetPartName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index);

// part: 部位の名前 ("SSG" 等)
// 実際に掛かるゲインは FmEngine_SetGain のゲイン × 部位のゲイン
// 未知の chip_id、チップが持たない部位の名前、nullptr は FM_ERR_INVALID_ARG
// オーディオコールバックスレッドと並行して呼び出し可能
FmResult FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float gain_l, float gain_r);
FmResult FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float* out_gain_l, float* out_gain_r);
```

```c
// 使用例: OPNA が持つ部位をすべて表示し、SSG を -6 dB にする
typedef uint32_t    (*PFN_GetPartCount)(FmEngineHandle, uint32_t);
typedef const char* (*PFN_GetPartName)(FmEngineHandle, uint32_t, uint32_t);
typedef FmResult    (*PFN_SetPartGain)(FmEngineHandle, uint32_t, const char*, float, float);
typedef FmResult    (*PFN_GetPartGain)(FmEngineHandle, uint32_t, const char*, float*, float*);

PFN_GetPartCount getPartCount = (PFN_GetPartCount)GetProcAddress(dll, "FmEngine_GetPartCount");
if (getPartCount) {
    PFN_GetPartName getPartName = (PFN_GetPartName)GetProcAddress(dll, "FmEngine_GetPartName");
    PFN_SetPartGain setPartGain = (PFN_SetPartGain)GetProcAddress(dll, "FmEngine_SetPartGain");
    PFN_GetPartGain getPartGain = (PFN_GetPartGain)GetProcAddress(dll, "FmEngine_GetPartGain");

    uint32_t n = getPartCount(eng, opna_id);
    for (uint32_t i = 0; i < n; ++i) {
        const char* name = getPartName(eng, opna_id, i);
        float l, r;
        getPartGain(eng, opna_id, name, &l, &r);
        printf("%s: L=%.2f R=%.2f\n", name, l, r);
    }

    // 名前が分かっている部位は直接指定できる。チップが持たなければ FM_ERR_INVALID_ARG
    setPartGain(eng, opna_id, "SSG", 0.5f, 0.5f);
}
```

### 部位の名前

次のチップに部位を持たせるエンジンは、名前と既定値をこの表のとおりにします。エンジンを切り替えても、同じ名前で同じ出力を指定できます。

| 対象チップ | 部位の名前 | 内容 | 既定値 |
|---|---|---|---|
| OPN, OPNA, OPNB, OPNBB | `FM` | FM 部 (ADPCM・リズムを含む) | 1.0 |
| OPN, OPNA, OPNB, OPNBB | `SSG` | SSG 部 | 1.0 |
| OPLL, OPLLP, OPLLX, VRC7 | `MELODY` | メロディ | 1.0 |
| OPLL, OPLLP, OPLLX, VRC7 | `RHYTHM` | リズム | 1.0 |
| OPL3 | `AB` | 出力 A (L) / B (R) | 1.0 |
| OPL3 | `CD` | 出力 C (L) / D (R) | 0 |
| OPL4 | `DO0` | DO0 (FM の C/D) | 0 |
| OPL4 | `DO1` | DO1 (AWM の C/D) | 0 |
| OPL4 | `DO2` | DO2 (FM の A/B と AWM の A/B のミックス) | 1.0 |

- 部位の名前はチップごとに独立です。別のチップの部位の名前を渡すと `FM_ERR_INVALID_ARG` を返します。
- 出力が1系統のチップ (OPL, OPL2, Y8950, OPN2, OPM, OPZ など) は部位を持たず、`FmEngine_GetPartCount` は 0 を返します。ゲインは `FmEngine_SetGain` で設定します。
- 表のチップでも、エンジンによっては部位を持ちません (この節の関数をエクスポートしないエンジンなど)。`FmEngine_GetPartCount` で確かめてください。
- 表に無いチップの部位は、名前と既定値をエンジンが決めます。呼び出し側は列挙して取得できるので、この仕様書とヘッダの変更は要りません。同じチップを複数のエンジンが実装するときは、表に足して名前を揃えます。
- 名前は ASCII の英大文字・数字・`_` で付けます。
- 既定値は `FmEngine_AddChip` の直後に `FmEngine_GetPartGain` で取得できます。
- 部位を並べる順序 (`index`) は定めません。エンジンは、同じ chip_id には `FmEngine_Destroy` まで同じ順序で同じ名前を返します。設定ファイルなどに部位を書き残すときは、`index` ではなく名前を使ってください。
- C/D 側 (OPL3 の `CD`、OPL4 の `DO0` と `DO1`) の既定値が 0 なのは、FM の出力先を A/B/C/D 全部にしたチャンネルが A/B と C/D に同じ音を出し、混ぜると二重に足されるためです。

## 外部メモリ (任意)

チップによっては、音源コアの外にあるメモリを読み書きします (ADPCM の ROM や RAM など)。ここでいう「外部メモリ」は**音源コアの外部**という意味で、必ずしも物理的にチップの外側に存在することを意味しません。  
外部メモリはキーワード文字列で識別します (大文字小文字を区別する)。チップが持つ外部メモリはエンジンに問い合わせて取得するので、ヘッダに定数は不要です。

この節の関数は任意のエクスポートです。エクスポートしていない DLL もこの仕様に準拠します。  
エクスポートするときは、3 つを必ず組にします。呼び出し側は `GetProcAddress` / `dlsym` で `FmEngine_GetMemoryCount` の有無を確かめ、無ければ `FmEngine_SetMemory` も [`FmEngine_SetMemoryEx`](#外部メモリの割り当て-任意) も呼ばず、どのチップも外部メモリを持たないものとして扱ってください。`FmEngine_GetMemoryCount` を持たずに `FmEngine_SetMemory` / `FmEngine_SetMemoryEx` をエクスポートする DLL は、第 3 引数が文字列ではなく、この仕様と互換性がありません。

```c
// チップが持つ外部メモリの数。持たないチップと未知の chip_id は 0
uint32_t    FmEngine_GetMemoryCount(FmEngineHandle engine, uint32_t chip_id);

// index 番目の外部メモリの名前。範囲外と未知の chip_id は nullptr
// 文字列は FmEngine_Destroy が戻るまで有効
const char* FmEngine_GetMemoryName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index);

// memory: 外部メモリの名前 ("ADPCM_B" 等)
// 未知の chip_id、チップが持たないメモリの名前、memory が nullptr なら FM_ERR_INVALID_ARG
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// data の寿命は呼び出し元が管理すること
FmResult FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    const char* memory, const uint8_t* data, uint32_t size);
```

`FmEngine_SetMemory` の `data` は書き込み可能なメモリであるとは限りません。エンジンは `data` に書き込みません。
エンジンがデータを複製するか参照するか、複製した場合にチップからの書き込みを複製に反映するかは、エンジンのコア実装によります。
ROM と RAM を区別して割り当てるには [`FmEngine_SetMemoryEx`](#外部メモリの割り当て-任意) を使います。

```c
// 使用例: チップが持つ外部メモリを問い合わせ、名前に合う ROM イメージを渡す
typedef uint32_t    (*PFN_GetMemoryCount)(FmEngineHandle, uint32_t);
typedef const char* (*PFN_GetMemoryName)(FmEngineHandle, uint32_t, uint32_t);
typedef FmResult    (*PFN_SetMemory)(FmEngineHandle, uint32_t, const char*, const uint8_t*, uint32_t);

PFN_GetMemoryCount getMemoryCount = (PFN_GetMemoryCount)GetProcAddress(dll, "FmEngine_GetMemoryCount");
if (getMemoryCount) {
    PFN_GetMemoryName getMemoryName = (PFN_GetMemoryName)GetProcAddress(dll, "FmEngine_GetMemoryName");
    PFN_SetMemory     setMemory     = (PFN_SetMemory)GetProcAddress(dll, "FmEngine_SetMemory");

    uint32_t n = getMemoryCount(eng, chip_id);
    for (uint32_t i = 0; i < n; ++i) {
        const char* name = getMemoryName(eng, chip_id, i);
        const uint8_t* image;
        uint32_t       size;
        // find_image: アプリケーションが持つ ROM イメージを、チップ名とメモリの名前で探す
        if (find_image(chip_name, name, &image, &size))
            setMemory(eng, chip_id, name, image, size);
    }
}
```

### 外部メモリの名前

次のチップに外部メモリを持たせるエンジンは、名前をこの表のとおりにします。エンジンを切り替えても、同じ名前で同じメモリを指定できます。

| 対象チップ | 外部メモリの名前 | 内容 |
|---|---|---|
| OPNA | `RHYTHM` | リズム音の内蔵 ROM の内容 |
| OPNA, Y8950 | `ADPCM_B` | ADPCM-B の ROM/RAM 選択ビットが RAM のときにアクセスするメモリ |
| OPNA, Y8950 | `ADPCM_B_ROMMODE` | ADPCM-B の ROM/RAM 選択ビットが ROM のときにアクセスするメモリ |
| OPNB, OPNBB | `ADPCM_A` | ADPCM-A のメモリ |
| OPNB, OPNBB | `ADPCM_B` | ADPCM-B のメモリ |
| OPL4 | `PCM` | PCM のメモリ |

OPNA と Y8950 の ROM/RAM 選択ビットはアクセスの方法を切り替えるもので、ROM モードと RAM モードでは別のメモリにアクセスします。ROM モードのメモリに RAM をつなぐこともできるので、選択ビットはメモリが書き込めるかどうかを表しません。

- 外部メモリの名前はチップごとに独立です。別のチップのメモリの名前を渡すと `FM_ERR_INVALID_ARG` を返します。
- 外部メモリを持たないチップでは、`FmEngine_GetMemoryCount` は 0 を返します。
- 表のチップでも、エンジンによっては表のメモリの一部しか持たないか、外部メモリを持ちません。`FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` で確かめてください。
- `FmEngine_GetMemoryName` が返す名前は、どれも `FmEngine_SetMemory` に渡せます。`FmEngine_SetMemoryEx` をエクスポートするエンジンでは、`FmEngine_SetMemoryEx` にも渡せます。
- 表に無いチップの外部メモリは、名前をエンジンが決めます。呼び出し側は列挙して取得できるので、この仕様書とヘッダの変更は要りません。同じチップを複数のエンジンが実装するときは、表に足して名前を揃えます。
- 名前は ASCII の英大文字・数字・`_` で付けます。
- 外部メモリを並べる順序 (`index`) は定めません。エンジンは、同じ chip_id には `FmEngine_Destroy` まで同じ順序で同じ名前を返します。設定ファイルなどに外部メモリを書き残すときは、`index` ではなく名前を使ってください。

## 外部メモリの割り当て (任意)

この節の関数は任意のエクスポートです。エクスポートしていない DLL もこの仕様に準拠します。エクスポートするエンジンは、[外部メモリ](#外部メモリ-任意)の 3 関数もエクスポートします。  
呼び出し側は、`FmEngine_GetMemoryCount` があることを確かめた上で、`GetProcAddress` / `dlsym` で `FmEngine_SetMemoryEx` の有無を確かめてから呼び出してください。シンボルが無いエンジンでは `FmEngine_SetMemory` だけが使えます。  
エクスポートするかどうかの判断基準は[節の末尾](#エクスポートするかどうかの判断基準)を参照してください。

```c
// memory: 外部メモリの名前 ("ADPCM_B" 等)
// memory のメモリの [base, base + size) に data を割り当てる
// data == NULL ならその範囲と重なる割り当てをすべて外す (access は無視)
// ストリーム開始前に呼ぶこと (スレッドセーフではない)
// 戻り値:
//   FM_ERR_INVALID_ARG : 未知の chip_id、チップが持たないメモリの名前、memory が nullptr、
//                        size が 0、既存の割り当てと範囲が重なる
//   FM_ERR_UNAVAILABLE : FM_ACCESS_RAM のブロックをその場で読み書きできない
FmResult FmEngine_SetMemoryEx(
    FmEngineHandle engine, uint32_t chip_id,
    const char* memory, uint32_t base,
    uint8_t* data, uint32_t size, FmMemoryAccess access);
```

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
FmEngine_GetSampleRate
FmEngine_Write
FmEngine_SetGain
FmEngine_GetGain
FmEngine_Generate
```

DLL がエクスポートしてもよいシンボル (任意。エクスポートするときは 4 つを組にする。[部位ごとのゲイン](#部位ごとのゲイン-任意)):

```
FmEngine_GetPartCount
FmEngine_GetPartName
FmEngine_SetPartGain
FmEngine_GetPartGain
```

DLL がエクスポートしてもよいシンボル (任意。エクスポートするときは 3 つを組にする。[外部メモリ](#外部メモリ-任意)):

```
FmEngine_GetMemoryCount
FmEngine_GetMemoryName
FmEngine_SetMemory
```

DLL がエクスポートしてもよいシンボル (任意。エクスポートするときは上の 3 つもエクスポートする。[外部メモリの割り当て](#外部メモリの割り当て-任意)):

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
    [DllImport(DLL)] public static extern uint    FmEngine_GetSampleRate(IntPtr engine);
    [DllImport(DLL)] public static extern int     FmEngine_Write(
        IntPtr engine, uint chipId, byte reg, byte value, uint port);
    [DllImport(DLL)] public static extern int     FmEngine_SetGain(
        IntPtr engine, uint chipId, float gainL, float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_GetGain(
        IntPtr engine, uint chipId, out float gainL, out float gainR);
    // 任意シンボル。DLL が FmEngine_GetPartCount をエクスポートしているときだけ呼ぶこと
    [DllImport(DLL)] public static extern uint    FmEngine_GetPartCount(IntPtr engine, uint chipId);
    [DllImport(DLL)] public static extern IntPtr  FmEngine_GetPartName(
        IntPtr engine, uint chipId, uint index);
    [DllImport(DLL)] public static extern int     FmEngine_SetPartGain(
        IntPtr engine, uint chipId, string part, float gainL, float gainR);
    [DllImport(DLL)] public static extern int     FmEngine_GetPartGain(
        IntPtr engine, uint chipId, string part, out float gainL, out float gainR);
    // 任意シンボル。DLL が FmEngine_GetMemoryCount をエクスポートしているときだけ呼ぶこと
    // data は呼び出しの後もエンジンが使うことがあるので、GC が動かさないメモリ
    // (Marshal.AllocHGlobal、固定した GCHandle など) を渡すこと
    [DllImport(DLL)] public static extern uint    FmEngine_GetMemoryCount(IntPtr engine, uint chipId);
    [DllImport(DLL)] public static extern IntPtr  FmEngine_GetMemoryName(
        IntPtr engine, uint chipId, uint index);
    [DllImport(DLL)] public static extern int     FmEngine_SetMemory(
        IntPtr engine, uint chipId, string memory, IntPtr data, uint size);
    // 任意シンボル。FmEngine_GetMemoryCount に加えて、これもエクスポートしているときだけ呼ぶこと
    [DllImport(DLL)] public static extern int     FmEngine_SetMemoryEx(
        IntPtr engine, uint chipId, string memory, uint baseAddr,
        IntPtr data, uint size, int access);
    [DllImport(DLL)] public static extern int     FmEngine_Generate(
        IntPtr engine, IntPtr outL, IntPtr outR, uint samples);
}
```
