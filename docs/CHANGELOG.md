# CHANGELOG

開発経緯の記録。現在の仕様は `README.md` と `docs/` 以下の仕様書を参照。

## FmEngineApi 仕様書を YMEngine の部位ゲイン追加に合わせる

YMEngine `8f81213` の `src/FmEngineApi.h` / `src/FmEngineApi.def` に合わせて
`docs/FmEngineApi.md` を更新した。

- 追加：`FmPart` と `FmEngine_SetPartGain` / `FmEngine_GetPartGain` /
  `FmEngine_GetPartMask`
- `FmEngine_GetNativeRate` は FM 部のレートを返す（OPN/OPNA では prescale で
  変わる）。YMEngine は以前、OPN 系で FM と SSG をまとめた列のレートを返していた
- 前からヘッダと食い違っていた箇所：`FM_MEM_ADPCM_B` の対象チップ（Y8950 を
  含む）、`FmEngine_SetGain` / `FmEngine_GetGain` がオーディオコールバックと
  並行して呼べること

### 決めたこと（利用者と決めた）

部位ゲインの3関数は任意のエクスポートにする。エクスポートしていない DLL も
準拠とする。`FmPart` の番号はこの仕様書で割り当てる。

前提：FMEngineTest が部位ゲインを使わず、YMEngine 以外の互換エンジンが部位
ゲインを実装していないこと。テストツールで部位ゲインを試験するようになったら、
必須にするかを決め直す。

やり直しの値段：必須に上げるのは仕様書の節を移すだけ。ただし上げた時点で、
未実装のエンジンは非準拠になる（スタブで準拠にできる：`GetPartMask` は 0、
`Set/GetPartGain` は `FM_ERR_INVALID_ARG`）。

見送った案：

- 必須にする。理由：YMEngine 以外の互換エンジンがすべて非準拠になる。
  FMEngineTest も使っていない
- 仕様書に載せず、YMEngine 固有の拡張として扱う。理由：別のエンジンが部位を
  足すときに番号を独自に振ると、同じ番号が別の部位を指す

### 確認

**確認済み**（ソースを検索）：

- FMEngineTest の `src/main.cpp` は必須14シンボルをすべて `LOAD_SYM` で読み、
  1つでも欠けると DLL のロードを失敗にする。部位ゲインの関数は読んでいない
- NukedEngine `cd53ff6`、FMgenEngine `3ba7f6d`、DSAemuEngine `0f5c786`、
  SAASoundEngine `4173159` のソース（`extern/` を除く）に `SetPartGain` /
  `GetPartMask` / `FmPart` は無い

DBOPLEngine と SCCIBridgeEngine は**未確認**（手元にソースが無かった）。
