# CHANGELOG

開発経緯の記録。現在の仕様は `README.md` と `docs/` 以下の仕様書を参照。

## 外部メモリの ROM/RAM を区別する（FmEngine_SetMemoryEx）

目的：RAM として渡したメモリブロックを、他のデバイスと共有できるようにする。
`FmEngine_SetMemory` は ROM と RAM を区別せず、エンジンごとに扱いが違う。
仕様は `docs/FmEngineApi.md` の「外部メモリの割り当て (任意)」に書いた。
仕様書を参照実装より先に書いた。YMEngine を含め、まだどのエンジンも実装していない。

### 現状

**確認済み**（ソースを読んだ。YMEngine `6b8dba9`、FMgenEngine `c74f672`、
DSAemuEngine `1d11be7`、DBOPLEngine `17b267f`、NukedEngine `cd53ff6`、
SAASoundEngine `4173159`）：

- YMEngine：ポインタを参照する。チップからの書き込みは捨てる
  （`writeable=false`）。割り当てが無いときも捨てる
- FMgenEngine：OPNA は fmgen 内部の 256KB バッファに複製する。OPNB/OPNBB は
  ポインタを参照する（チップは読むだけ）
- DSAemuEngine：Y8950 は emu8950 内部の RAM に複製する
- DBOPL / Nuked / SAASound：`FM_ERR_UNAVAILABLE`
- ROM/RAM 選択ビットの扱い：ymfm はアドレスの刻み（ROM と x8 は 32 バイト、
  x1 は 4 バイト）を変えるだけで、メモリは 1 つ。emu8950 は ROM と RAM を別の
  バッファに持ち、ビットで切り替える。fmgen はビットを見ない。3 つとも ROM
  モード中のレジスタ経由の書き込みを止めていない

**未検証**：YMEngine では、OPNA/Y8950 の ADPCM-B にレジスタ経由で転送した
データは鳴らない（上のコードからの判断。鳴らしていない）。

### 前提（ハードウェア）

利用者から：Y8950/YM2608 の ROM/RAM 選択ビットはメモリのアクセス方法そのものを
切り替える。ROM と RAM は物理的に別のメモリ。ROM モードでも RAM をつなげるので、
このビットはメモリが書き込めるかを表さない。ROM モードでレジスタ経由の書き込みが
できないのはチップの動作による。YM2608 の x8 モードは、Y8950 の ROM モードで
/WE が出るようにしたもの。

**確認済み**（Y8950 アプリケーションマニュアル III-7「外部メモリー・コントロール」、
IV-2「外部メモリー・インターフェイス」の本文と図IV-3を読んだ）：

- 外部メモリは RAM・ROM とも最大 256KB
- ROM：`/ROM-CS` で選ぶ。アドレスは DM0–7・A8 の多重化出力を `/RAS`・`/CAS` で
  外部ラッチして与える。アクセスはバイト単位、アドレス指定は 32 バイト単位。
  図IV-3 では ROM に `/WE` はつながっていない。データは D0 が DTO、D1–7 が DM1–7
- RAM：64K または 256K の D-RAM（1 ビット幅）を 8 個まで。8 個はアドレス・
  `/RAS`・`/CAS`・`/WE` を共有し、RAM n の DIN は DM(n−1)、DO は MDEN で開く
  トライステートバッファを通って RAM1 が DTO、RAM2–8 が DM1–7 に出る。
  アクセスは RAM1 から RAM8 へ順にビット単位のシリアルで、アドレス指定は
  4 バイト単位。D-RAM 1 個の最小構成では外部回路は要らない
- メモリ書き込みの手順例は `$08` に `$00`/`$02` だけを書き、読み出しの手順例は
  `$00`/`$01`/`$02` を書く（`$01` が ROM）
- ymfm のアドレスの刻み（ROM 32 バイト、x1 RAM 4 バイト）はマニュアルと一致する

### 決めたこと（利用者と決めた）

- 任意のエクスポート `FmEngine_SetMemoryEx` を足す。`FmEngine_SetMemory` は残す
- `FmMemoryAccess` はつないだデバイスの種類を表す
  - `FM_ACCESS_ROM = 0`：割り当て中は内容が変わらない。エンジンは複製してよい。
    チップからの書き込みは捨てる
  - `FM_ACCESS_RAM = 1`：チップ以外も書き換えてよい。エンジンは複製せず、その場で
    読み書きする。できないエンジンは `FM_ERR_UNAVAILABLE` を返す
  - チップがいつ書き込むか（ROM モード中は書かない等）はチップの動作で、この属性
    では決まらない
- `FmMemoryType` に `FM_MEM_ADPCM_B_ROMMODE = 4`（OPNA・Y8950 が ROM モードで
  アクセスするメモリ）を足す。`SetMemoryEx` でだけ使う。古い YMEngine の
  `SetMemory` に 4 を渡すと、エラーにならずに ADPCM-B へ割り当てられるため
- OPNA・Y8950 の `FM_MEM_ADPCM_B` は RAM モードでアクセスするメモリとする
- `base` 引数（そのメモリでの先頭の番地）を持たせる。ymfm の OPL4 は PCM の
  読み書きを 1 つの空間に流すので、ROM と SRAM が同じ空間に並ぶ構成を表すには
  範囲が要る。D-RAM 1 個だけの Y8950 のように、一部だけにメモリがある構成も
  表せる。OPL4 の実際の配置は**推測**（確かめていない）
- `data == NULL` で割り当てを外す
- RAM のブロックに対してエンジンが守ること：触るのは `SetMemoryEx`・`Write`・
  `Generate` の中だけ。`Write(X)` より前に呼び出し側が書いた値は X の反映時に
  チップから見える。X によるチップの書き込みは、X の後に始まった `Generate` が
  戻った時点でブロックに入っている。それ以外の並行アクセスは保証しない。別の
  デバイスと共有するときは、エンジンを止めるなどの手当てを呼び出し側がする
- 既存の `SetMemory` は、複製するか、チップの書き込みを反映するかをエンジンの
  コア実装に任せる
- ブロックのバイトの並びは API で定めない。並びは基板の配線やアクセス方式
  （Y8950 RAM のビットシリアル、YM2608 の x8）で決まるもので、アプリケーションと
  エンジンの間で合意するしかなく、API は介入できない

前提：RAM の割り当てはストリーム開始前に決まり、動作中に切り替えない。崩れたら
割り当てをスレッドセーフにする必要がある。

やり直しの値段：名前や値の変更は、エンジンが実装する前なら仕様書だけ。実装した
後は各エンジンのリポジトリにも及ぶ。FMEngineTest の `src/main.cpp` は
`SetMemoryEx` も `FM_MEM_ADPCM_B_ROMMODE` も使っていない。

YMEngine が実装すると挙動が変わる：OPNA に `SetMemory(FM_MEM_ADPCM_B)` で ROM
イメージを渡して ROM モードで鳴らしている呼び出し側は、無音になる。FMEngineTest の
`src/main.cpp` は ADPCM-B を OPNB/OPNBB にだけ渡す（確認済み：ソースを読んだ）。

### 仕様書に書いたが、利用者と明示的には決めていないこと

最初の案に入れたまま仕様書に書いた。変えるときは仕様書の該当行だけで済む
（実装しているエンジンがまだ無いため）。

- 割り当ての無い番地を読むと 0、書き込みは捨てる
- 既存の割り当てと範囲が重なると `FM_ERR_INVALID_ARG`
- `data == NULL` のときは、範囲と重なる割り当てをすべて外す
- チップが持たない `mem_type` には `FM_ERR_INVALID_ARG`（部位ゲインと同じ扱い）
- YM2608 の x8 モードでアクセスするメモリ。仕様書は ROM/RAM 選択ビットで分けて
  いるので `FM_MEM_ADPCM_B` になる。x8 モードのアクセス方法は ROM モードと同じ
  なので、実機で ROM モードと同じメモリが応答するなら `FM_MEM_ADPCM_B_ROMMODE`
  にするべき。未確認

### 見送った案

- フラグを `FmMemoryType` の上位ビットに入れ、`SetMemory` の形を変えない。
  理由：古い DLL が未知の値を黙って受け付ける（YMEngine は ADPCM-B として扱い、
  FMgen は IO として扱って `FM_OK` を返す）
- `SetMemory` の引数を変える。理由：必須シンボルが変わり、全エンジンと
  `src/main.cpp` を同時に直す必要がある
- エンジンが RAM を確保してポインタを返す。理由：呼び出し側が持つブロックを
  共有するという要件と向きが逆で、複数のチップで 1 つの RAM を共有できない
- RAM を「共有する」と「共有しない（複製してよい）」に分ける。理由：複製して
  よい RAM は、FMgen と DSAemu が今の `SetMemory` で実現している
- ROM モードと RAM モードのメモリを 1 つの空間で扱う（ymfm の持ち方）。
  理由：実機では別のメモリ
- ROM モード側の名前を `FM_MEM_ADPCM_B_ROM` にする。理由：「ROM のデバイス」と
  読める。ROM モードでアクセスするメモリにも RAM をつなげる
- ブロックのバイトの並びを API で定める（チップが読む論理アドレス順）。理由：
  上の「決めたこと」のとおり、並びはアプリケーションとエンジンの間の合意事項
- 割り当ての解除を別の関数（`FmEngine_UnmapMemory`）にする。理由：利用者が
  `data == NULL` を選んだ

**未検証**：fmgen の `adpcmbuf`（`OPNABase` の protected メンバ）や emu8950 の
`memory[0]`/`memory[1]`（公開構造体のメンバ）を呼び出し側のブロックに差し替えて、
RAM をその場で扱えるか。どちらもコアが 256KB を前提に添字を作るので、それより
小さいブロックは拒否するか、コアを直す必要がある。

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
