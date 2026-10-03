# CHANGELOG

開発経緯の記録。現在の仕様は `README.md` と `docs/` 以下の仕様書を参照。

## FmEngine_GetNativeRate を廃止する

利用者の指摘：`FmEngine_GetNativeRate` は、何を返すかがあいまいで、
アプリケーションの側にも使い道が無い。

### 変更前

**確認済み**（2026-10-03 に各リポジトリを fetch し、`origin/HEAD` を `git grep` で
検索して、実装を読んだ。YMEngine `7d8ed2d`、NukedEngine `933f16d`、FMgenEngine
`34079ec`、DSAemuEngine `85a7276`、DBOPLEngine `c67d84f`、EPSGemuEngine `7cd1e8a`、
DSGemuEngine `a1894f5`、SAASoundEngine `bb0c0cd`、SCCIBridgeEngine `808065e`、
Y8960emu `da2ab34`、FitomEmuIF `d81075c`、FITOM_X `29b2c31`、FITOMApp `dbb7c25`、
Y8960Sequencer `370f7d4`）：

- 実装しているエンジンは 10 本で、返すものが揃っていない
  - YMEngine：FM 部を生成するレート（OPN/OPNA では prescale の書き込みで変わる）
  - FMgenEngine：実機の FM 部のレート。生成には使っていない。SSG は clock / 16
  - DBOPLEngine：ホストのサンプルレート
  - SAASoundEngine：clock / 512
  - SCCIBridgeEngine：チップのクロックそのもの
  - NukedEngine、DSAemuEngine、EPSGemuEngine、DSGemuEngine、Y8960emu：チップごとに
    持っている値を返す（何の値かは読んでいない）
- アプリケーション（FitomEmuIF、Y8960Sequencer、FITOM_X、FITOMApp）は呼んで
  いない。FitomEmuIF にあるのは、ヘッダの写しの宣言と、テスト用のスタブ
  エンジンのエクスポートだけ
- 呼ぶのは、エンジン自身のテスト（YMEngine、FMgenEngine、DSAemuEngine、
  DBOPLEngine、Y8960emu）と、このリポジトリの `src/main.cpp`。`main.cpp` は必須
  シンボルとして読み込み、チップごとのログに `native_rate=` として出していた

**確認済み**（変更前のツールで `patches/all.json` を走らせ、ログを読んだ）：返る値は
YMFMEngine.dll が 44,100〜55,930 の 6 通り、DSAemuEngine.dll が 49,715 /
223,721 / 447,443 / 1,789,772、SAASoundEngine.dll が 15,625。

### 決めたこと（利用者と決めた）

`FmEngine_GetNativeRate` を仕様から外す。必須シンボルは 12 個から 11 個になる。

前提：アプリケーションが使っていないこと（上の確認の範囲）。

やり直しの値段：同じ名前で戻すのは、仕様書・ヘッダ・`src/main.cpp` に行を足す
だけ。ただし、戻した時点で、エクスポートをやめたエンジンは非準拠になる。意味を
決めた別の関数を任意で足すなら、互換は壊れない。

### 仕様書・ヘッダに書いていないが、こう扱ったこと

- `src/main.cpp` のチップごとのログは `[OPL2] chip_id=0` になる。エンジンが
  `FmEngine_GetNativeRate` をエクスポートしていても、呼ばない
- クロックがエンジンに渡ったことを、ログで確かめられなくなる。「FmEngine_AddChip の
  clock=0（標準クロック）を廃止する」の節は、`native_rate` が変わることを根拠に
  していた。以後は、書き出した WAV の音程で確かめる
- 仕様書は、一覧に無いシンボルのエクスポートを禁じていない。エンジンが
  `FmEngine_GetNativeRate` を残していても準拠する

### 見送った案

- 意味を決め直して残す（FM 部を生成するレート、などと定める）。理由：利用者が
  廃止を選んだ。アプリケーションに使い道が無い

### エンジンとアプリケーション側の対応（まだ）

- 10 本のエンジン：ヘッダを `include/FmEngineApi.h` の写しにし、
  `FmEngine_GetNativeRate` のエクスポートと、それを呼ぶテストをやめる
- FitomEmuIF：ヘッダの写しと、テスト用のスタブエンジンを直す

対応前のエンジンは、新しい呼び出し側からそのまま使える。逆に、この変更より前の
FMEngineTest は、エクスポートをやめたエンジンをロードできない
（`FmEngine_GetNativeRate` を必須として読むため）。

### 確認

**確認済み**：

- `cmake/CheckApiSymbols.cmake`：ヘッダと仕様書が 19 シンボルで一致する（必須 11、
  部位 4、外部メモリ 3、外部メモリの割り当て 1）
- `src/main.cpp` を MSVC 19.44（x64、Release）でビルドできる
- 検証用の最小のエンジン（リポジトリには入れていない）と、変更前（`4006d3e`）・
  変更後のツールの組み合わせ：
  - `FmEngine_GetNativeRate` をエクスポートしないエンジン：変更後のツールは
    ロードして最後まで走る。変更前のツールは `FmEngine_GetNativeRate not found in
    DLL` でロードに失敗する（対照）
  - エクスポートを残したエンジン（呼ばれたら記録する）：変更後のツールでは記録が
    出ない。変更前のツールでは、チップの数だけ記録が出る（対照）
- 変更前と変更後のツールで `patches/all.json` を WAV に書き出すと、バイト一致
  する。YMFMEngine.dll、DSAemuEngine.dll、SAASoundEngine.dll で比べた。どれも
  無音ではない。ログの違いは `, native_rate=… Hz` の有無だけ。DLL は手元の
  ビルドにあったもので、どのコミットからビルドされたかは確かめていない
- ヘッダは MSVC の C（`/TC`）と C++（`/TP`）の両方で通り、19 関数すべてを、
  仕様書の引数を書いた関数ポインタに代入できる。`FmEngine_GetNativeRate` を使う
  コードはコンパイルできない。変更前のヘッダでは通る（対照）
- 「外部メモリを名前で指定する」の節で未検証だった、実際のエンジンでの問い合わせ：
  名前で指定する形をエクスポートする手元のビルド（DSAemuEngine.dll と
  FmGenEngine.dll）に変更後のツールを当てると、仕様書の表の名前が返る。
  DSAemuEngine の Y8950 は `ADPCM_B` と `ADPCM_B_ROMMODE`、FMgenEngine の OPNA は
  `ADPCM_B` と `ADPCM_B_ROMMODE`、OPNB と OPNBB は `ADPCM_A` と `ADPCM_B`。ROM
  ファイルは置いていないので、実際のエンジンへの `FmEngine_SetMemory` は通って
  いない

**未検証**：

- GCC / Clang でのビルド（手元に無い）
- 仕様書の C# サンプル（コンパイルしていない）

## 依存ライブラリを vcpkg から取る

利用者の要望：nlohmann/json と RtAudio を vcpkg 経由にしたい。

### 変更前

**確認済み**（2026-10-03 に `CMakeLists.txt` とサブモジュールを読んだ）：

- 2 つとも git サブモジュール（`extern/nlohmann_json`、`extern/rtaudio`）だった。
  RtAudio は `add_subdirectory` でビルドし、nlohmann/json は `single_include` を
  include パスに足していた
- RtAudio は `e5f0774`（2026-02-27）。タグ 6.0.1（2023-08-01）から 72 コミット後
- nlohmann/json は `25c58ac`（2026-06-23）。develop の途中で、タグ v3.12.0 とは
  互いに祖先でない。ヘッダのバージョンは 3.12.0
- Windows では、RtAudio のバックエンドは WASAPI だけで、RtAudio は DLL

### 決めたこと（利用者と決めた）

- 依存は `vcpkg.json`（マニフェスト）に書き、`CMakeLists.txt` は `find_package` で
  探す。サブモジュールと `.gitmodules` は外す。どのプラットフォームでも vcpkg を
  前提にする
- RtAudio は vcpkg のポートの版（6.0.1）をそのまま使う
- README のビルド手順は、今までのコマンドに `-DCMAKE_TOOLCHAIN_FILE` を足す形に
  する。`CMakePresets.json` は足さない
- Linux のバックエンドは ALSA だけにする

前提：

- RtAudio の 6.0.1 から `e5f0774` までの修正が、このツールに要らないこと。
  マージを除いて 23 コミットが `RtAudio.cpp` / `RtAudio.h` を変えている。WASAPI に
  関わるのは 3 つで、COM スマートポインタへの書き換え（`028484c`）、その書き換えで
  入った出力が無音になる不具合の修正（`4f51d0a`）、入力側の 1 行の修正
  （`44d2f4a`）。6.0.1 は書き換えの前にあたる（**確認済み**：6.0.1 の
  `RtAudio.cpp` に `ComPtr` は 0 件、`e5f0774` には 34 件）。ほかに無くなるのは、
  ALSA のメモリリークの修正（`cad7908`）と S24_3LE 対応（`43a3211`）、CoreAudio の
  修正 2 つ（`9929045`、`b0e3374`）、PulseAudio の入力の遅延の改善（`b6edd70`）
  など。この一覧はコミットの題を読んだもので、差分は `4f51d0a` しか読んでいない
- ビルドする人が vcpkg を用意できること

やり直しの値段：

- サブモジュールに戻す：この変更を戻す（`CMakeLists.txt`、`README.md`、
  `.gitmodules`、`vcpkg.json`）
- RtAudio を別の版にする：オーバーレイポートを足す。`CMakeLists.txt` は
  変わらない。**未検証**：作っていない。vcpkg のポートが当てているパッチ
  （`fix-pulse.patch`）が新しいソースに当たるかも確かめていない
- 依存の版を上げる：`vcpkg.json` の `builtin-baseline` を 1 行変える
- プリセットを足す：`CMakePresets.json` を足すだけ。プリセット名は外に出る値に
  なる

### 利用者と明示的には決めていないこと

変えるときは `vcpkg.json` か `CMakeLists.txt` の数行で済む。

- `builtin-baseline` で vcpkg のポートの版を固定した（microsoft/vcpkg の
  `930ecc4`、2026-06-24）。RtAudio は 6.0.1（port-version 1）、nlohmann-json は
  3.12.0（port-version 2）になる。固定しないと、ビルドする人の vcpkg の版に
  よって依存の版が変わる。**推測**：vcpkg を浅いクローン（`--depth 1`）で用意した
  場合は、このコミットを読めずに止まる（vcpkg はベースラインの版の一覧を git の
  履歴から読む、という理解による。試していない）
- `vcpkg.json` に `name` と `version` は書いていない。`CMakeLists.txt` の
  `project()` と二重に持たないため
- トリプレットは vcpkg の既定のまま。Windows（`x64-windows`）では RtAudio は DLL
  で、ビルド時に exe の隣へコピーされる。Debug 構成の DLL は `rtaudiod.dll` に
  なる（今までは `rtaudio.dll`）
- Linux では RtAudio の `alsa` feature を指定した。vcpkg のポートは、feature で
  選ばなかった ALSA と PulseAudio を無効にし、JACK は常に無効にする（**確認済み**：
  ポートの `portfile.cmake` を読んだ）。今までは、RtAudio の `CMakeLists.txt` が
  ALSA を既定で有効にし、PulseAudio と JACK は見つかれば有効にしていた。
  PulseAudio は `vcpkg.json` に feature を 1 つ足せば有効になる。JACK は、ポートを
  変えない限り有効にできない
- vcpkg のツールチェーンを通さずに configure すると、案内を出して止まる。
  `find_package` に `REQUIRED` を付けず、見つからないときに自前のメッセージを出す。
  案内には、既存のビルドディレクトリでは `--fresh` が要ることも書いた。
  ツールチェーンを渡しているのに止まる場合が、これにあたる
- `FMEngineTest` の `DEBUG_POSTFIX ""` を外した。`add_subdirectory` した RtAudio が
  `CMAKE_DEBUG_POSTFIX` をキャッシュに書くのを打ち消すためのものだった

### 見送った案

- サブモジュールを残し、vcpkg のツールチェーンが無いときはそこからビルドする。
  理由：利用者が vcpkg だけにすることを選んだ。`CMakeLists.txt` に 2 経路が残り、
  版の違う RtAudio を両方確かめ続けることになる
- オーバーレイポートで、RtAudio を今までと同じコミット（`e5f0774`）に固定する。
  理由：利用者がポートの版を選んだ。ポートの保守がこのリポジトリに来る
- `CMakePresets.json` を足す。理由：利用者が、今のコマンドに引数を足す形を選んだ
- Linux で `pulse` feature も指定する。理由：今回の要望に無い

### 確認

**確認済み**（2026-10-03。Windows 10 x64、Visual Studio 17 2022 ジェネレータ、
MSVC 19.44、トリプレット `x64-windows`。変更後の作業ツリーをビルドして走らせた。
コールバックの回数だけは、同じ `rtaudio.dll` をリンクした別の小さなプログラムで
数えた）：

- README のコマンドの形（ビルド先のディレクトリ名だけ変えた）で configure でき、
  Release と Debug をビルドできる。`src/main.cpp` は変えていない。環境変数
  `CMAKE_TOOLCHAIN_FILE` が設定されていると、引数が無くても vcpkg が使われるので、
  この環境変数を外して走らせた
- ツールチェーンを渡さず、環境変数も外して configure すると、案内を出して止まる
- サブモジュールで configure したビルドディレクトリは、ツールチェーンを渡して
  configure し直しても、依存が見つからずに同じ案内を出して止まる。
  `cmake --fresh` を付けると通り、Release と Debug をビルドできる。Debug の
  出力先には、以前の `rtaudio.dll` と `rtaudio.pdb` が残る
- Debug の exe の名前に接尾辞は付かない
- 変更前の exe（サブモジュールでビルド）と変更後の exe で、WAV の書き出しが
  バイト一致する。YMFMEngine.dll で `patches/all.json`（48,000 Hz、183 秒）、
  DSAemuEngine.dll で `patches/all.json` と `patches/test_patches.json`
  （44,100 Hz）。ログも一致する。どちらも無音ではない。DLL は手元のビルドに
  あったもので、どのコミットからビルドされたかは確かめていない
- 変更後の exe をリアルタイム再生の経路で走らせると、ストリームが開いて始まる
  （チップの無いパッチを渡したので、音は出していない）
- vcpkg の RtAudio 6.0.1 で、`src/main.cpp` と同じ引数（WASAPI、48,000 Hz、
  32 ビット浮動小数、2 ch、512 フレーム）のストリームを開き、無音を書く
  コールバックを 2 秒回すと、192 回・98,304 フレーム呼ばれる。サブモジュールの
  RtAudio では 180 回・92,160 フレーム。出力デバイスの一覧は同じ
- ジェネレータで指定した版とは別のコンパイラを、vcpkg が選ぶことがある。
  確かめた環境（Visual Studio 2022 と、それより新しい版が入っている）では、
  vcpkg の `rtaudio.dll` は MSVC 14.51、exe は MSVC 14.44 でリンクされた（PE
  ヘッダのリンカのバージョンを読んだ）。上の確認は、この組み合わせで行った。
  サブモジュールのときは、どちらも 14.44 だった

**未検証**：

- 音が実際に聞こえること。上の確認は、無音を書くか、WAV に書き出して行った
- Linux と macOS でのビルド（手元にコンパイラが無い）。`vcpkg.json` の `alsa`
  feature は Windows では選ばれないので、Linux 向けの記述は一度も通っていない
- Linux で vcpkg が ALSA をビルドできること。vcpkg の `alsa` ポートは ALSA を
  ソースからビルドし、autoconf と libtool を要求する（ポートを読んだ）
- Linux でのリンクの形。vcpkg の既定のトリプレット（`x64-linux`）は静的リンク
  なので、ALSA（LGPL-2.1-or-later）が実行ファイルに静的にリンクされる
  （トリプレットとポートの定義を読んだ。ビルドはしていない）。今までは
  システムの共有ライブラリにリンクしていた。Linux のバイナリを配布するなら、
  ライセンスの条件を確かめる

## 外部メモリを名前で指定する

利用者の要望：外部メモリにも部位と同じ定数の問題がある。同じように、
アプリケーションが必要なメモリをエンジンに問い合わせる方式にしたい。

仕様は `docs/FmEngineApi.md` の「外部メモリ (任意)」と「外部メモリの割り当て
(任意)」、ヘッダは `include/FmEngineApi.h`。仕様書とヘッダを実装より先に書いた。
新しい形を実装したエンジンは、まだ無い。

### 変更前

**確認済み**（2026-10-03 に各リポジトリを fetch し、`origin/HEAD` を `git grep` で
検索して、該当する関数を読んだ。コミットは次の節と同じ）：

- 番号で指定する `FmEngine_SetMemory` / `FmEngine_GetMemorySize`（必須）を実装する
  エンジンは 10 本。メモリを実際に扱うのは YMEngine、FMgenEngine、DSAemuEngine、
  EPSGemuEngine、Y8960emu の 5 本。NukedEngine、DBOPLEngine、DSGemuEngine、
  SAASoundEngine は `FM_ERR_UNAVAILABLE` を返すスタブ。SCCIBridgeEngine は何もせずに
  `FM_OK` を返す
- `FmEngine_SetMemoryEx`（任意）でメモリを扱うのは YMEngine、FMgenEngine、
  DSAemuEngine、EPSGemuEngine。NukedEngine は `FM_ERR_INVALID_ARG` を返すスタブを
  エクスポートしている
- EPSGemuEngine は、3 種類のコア（ソースの `amm`・`adpcm`・`pcm`）のメモリを、
  どれも `FM_MEM_PCM` で受ける
- チップがそのメモリを持たないときの `FmEngine_SetMemory` の戻り値は、揃って
  いない。YMEngine と FMgenEngine は `FM_ERR_INVALID_ARG`、DSAemuEngine と
  EPSGemuEngine とスタブの 4 本は `FM_ERR_UNAVAILABLE`、SCCIBridgeEngine は
  `FM_OK`。仕様書は定めていなかった
- `FmEngine_GetMemorySize` をアプリケーションは呼んでいない。呼ぶのはエンジン
  自身のテストだけ（FMgenEngine、DSAemuEngine、DBOPLEngine）。返す値は揃って
  いない。YMEngine と EPSGemuEngine は割り当てたブロックの合計、DSAemuEngine と
  メモリを扱わない 5 本は常に 0。仕様書は意味を定めていなかった
- `FmEngine_SetMemory` を呼ぶアプリケーションは FitomEmuIF（チップ名から定数への
  対応表が 7 行、呼び出しが 1 か所）と Y8960Sequencer（呼び出しが 1 か所）。
  このリポジトリの `src/main.cpp` は `kRomTable`（5 行）で定数を引いていた。
  どれも「チップごとにどの定数を渡すか」を呼び出し側が表で持っている
- `FmEngine_SetMemoryEx` を呼ぶアプリケーションは無い

### 決めたこと（利用者と決めた）

- 外部メモリは名前の文字列で指定する。チップが持つ外部メモリは
  `FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` で列挙する
- 関数の名前は変えずに置き換える。`FmEngine_SetMemory` / `FmEngine_SetMemoryEx`
  の第 3 引数を `FmMemoryType` から `const char*` にし、`FmMemoryType` は無くす
- 外部メモリの関数は任意の組にする。必須シンボルは 14 個から 12 個になる。
  呼び出し側は `FmEngine_GetMemoryCount` の有無で判定する
- 名前は、今の定数名から `FM_MEM_` を取ったものにする（`ADPCM_A`、`ADPCM_B`、
  `ADPCM_B_ROMMODE`、`PCM`）。OPNA のリズム音の内蔵 ROM だけは、`ADPCM_A` では
  なく `RHYTHM` にする
- `FmEngine_GetMemorySize` は廃止する

前提：

- 番号で指定する形でビルドしたアプリケーション（今の FitomEmuIF と
  Y8960Sequencer）を、新しい形のエンジンと組み合わせて使わないこと。組み合わせて
  ROM を渡すと、DLL は番号をポインタとして読む
- 対応前のエンジンに ROM が渡らなくなってよいこと。新しい呼び出し側は、
  `FmEngine_GetMemoryCount` を持たない DLL を、外部メモリを持たないエンジンとして
  扱う。このテストツールも、対応前のエンジンには ROM ファイルを渡さなくなった

やり直しの値段：

- 関数や外部メモリの名前を変える：仕様書の 2 節とヘッダ、`src/main.cpp` の
  `kRomTable`、それに追随を済ませたエンジンとアプリケーション。アプリケーションが
  名前を設定ファイルに書き始めた後は、設定ファイルにも及ぶ
- 任意の組を必須に戻す：仕様書の節の見出しとシンボル一覧。戻した時点で、
  エクスポートしていないエンジンは非準拠になる

### 仕様書・ヘッダに書いたが、利用者と明示的には決めていないこと

変えるときは、仕様書の該当行とヘッダのコメントで済む（実装しているエンジンが
まだ無いため）。

- 組にするのは `FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` /
  `FmEngine_SetMemory` の 3 つ。`FmEngine_SetMemoryEx` は、その 3 つをエクスポート
  するエンジンがさらに足せる任意の関数にした。`FmEngine_SetMemoryEx` だけを
  エクスポートする形は認めない
- 列挙で返る名前は、どれも `FmEngine_SetMemory` に渡せる。変更前は
  `FM_MEM_ADPCM_B_ROMMODE` が `FmEngine_SetMemoryEx` 専用だった。専用にした理由は、
  古い YMEngine の `FmEngine_SetMemory` が未知の番号をエラーにせず受け付けること
  だった。新しい呼び出し側は `FmEngine_GetMemoryCount` の有無で古い DLL を
  見分けるので、この理由は無くなった。列挙した名前を順に渡すアプリケーションが、
  例外を持たずに済む
- チップが持たないメモリの名前と、`memory` が NULL のときは `FM_ERR_INVALID_ARG`
- `FmEngine_GetMemoryCount` は未知の chip_id に 0、`FmEngine_GetMemoryName` は
  範囲外と未知の chip_id に NULL を返す。名前の文字列は `FmEngine_Destroy` が戻る
  まで有効。`index` の順序は定めない。名前は ASCII の英大文字・数字・`_` で付ける
  （どれも部位と同じ）
- 仕様書の表にあるチップに外部メモリを持たせるエンジンは、表の名前を使う。表の
  メモリの一部しか持たなくてもよい。表に無いチップの外部メモリは、名前を
  エンジンが決める
- `FmMemoryAccess`（`FM_ACCESS_ROM` / `FM_ACCESS_RAM`）は残した。つないだデバイスの
  種類を表す値で、チップが増えても値は増えない
- `FmEngine_SetMemory` の `data` が NULL のときと `size` が 0 のときの扱いは、
  今回も定めていない。エンジンによって違う（DSAemuEngine は `data` が NULL で
  `size` が 0 でなければエラー、Y8960emu はどちらかでもエラー、EPSGemuEngine は
  `size` が 0 なら割り当てを外す）
- 仕様書の C# サンプルで、`FmEngine_SetMemory` の `data` を `byte[]` から `IntPtr`
  にした。`byte[]` は呼び出しの間しか固定されない。`data` を複製せずに参照する
  エンジンでは、呼び出しの後に動いたメモリを読むことになる
- `src/main.cpp`：エンジンが報告した外部メモリのうち、`kRomTable` に載っている
  ものに ROM ファイルを渡す。載っていないものは `[MEM]` の行で表示する。
  `FmEngine_GetMemoryCount` があるのに残りの 2 つが無い DLL は、ロードを失敗に
  する。`FmEngine_GetMemoryCount` が無い DLL には、断りを 1 行出す

### 見送った案

- 名前で指定する関数に別の名前を付け、番号で指定する 3 関数を仕様から外す、
  または旧式として残す。理由：利用者が同じ名前での置き換えを選んだ。旧式として
  残す場合は `FmMemoryType` の定数もヘッダに残る
- 必須のままにし、列挙の 2 関数も必須に足す（16 個）。理由：新しい
  アプリケーションが、対応前の DLL を 1 本もロードできなくなる。外部メモリを持つ
  チップが無いエンジンにもスタブが要る
- OPNA のリズム音の内蔵 ROM も `ADPCM_A` のままにする。理由：利用者が `RHYTHM` を
  選んだ。名前がチップごとに独立になったので、OPNB の名前を借りる必要が無い
- 問い合わせでアドレス空間の大きさも返す（`FmEngine_GetMemorySize` の意味を
  決め直す）。理由：利用者が名前だけを選んだ。要るようになったら関数を足せる
  （足すだけなら互換は壊れない）
- `FmEngine_GetMemorySize` を、引数だけ名前に変えて残す。理由：利用者が廃止を
  選んだ
- パッチ JSON に ROM ファイルの割り当てを書けるようにし、`kRomTable` を無くす。
  理由：今回の要望に無い。パッチのキーは外に出る値なので、足すときに決める

### エンジンとアプリケーション側の対応（まだ）

- メモリを扱う 5 本（YMEngine、FMgenEngine、DSAemuEngine、EPSGemuEngine、
  Y8960emu）：ヘッダを `include/FmEngineApi.h` の写しにする。
  `FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` を足す。`FmEngine_SetMemory`
  と `FmEngine_SetMemoryEx` は名前を受け取る。`FmEngine_GetMemorySize` をやめる。
  OPNA のリズム音の内蔵 ROM は `RHYTHM` にする。EPSGemuEngine は、今 `FM_MEM_PCM`
  で受けている 3 種類のメモリに名前を付ける
- スタブの 5 本（NukedEngine、DBOPLEngine、DSGemuEngine、SAASoundEngine、
  SCCIBridgeEngine）：外部メモリの関数のエクスポートをやめる
- FitomEmuIF と Y8960Sequencer：`FmEngine_SetMemory` を必須として読むのをやめ、
  `FmEngine_GetMemoryCount` の有無で判定する。定数を名前に置き換える

対応前のエンジンは `FmEngine_GetMemoryCount` を持たないので、新しい呼び出し側からは
外部メモリを持たないエンジンに見える。

### 確認

**確認済み**：

- `cmake/CheckApiSymbols.cmake`：ヘッダと仕様書が 20 シンボルで一致する（必須 12、
  部位 4、外部メモリ 3、外部メモリの割り当て 1）
- `src/main.cpp` を MSVC 19.44（x64、Release）でビルドできる
- 新しい形を実装した検証用の最小のエンジン（音は出さず、呼び出しを記録する。
  リポジトリには入れていない）を作り、テストツールから叩いた：
  - 新しい形：置いた ROM ファイルと、名前・大きさ・先頭と末尾のバイトが一致して
    渡る。OPNBB は、エンジンが返した順（`ADPCM_B`、`ADPCM_A`）で渡る。ROM
    ファイルを決めていないメモリ（OPNA の `ADPCM_B` と `ADPCM_B_ROMMODE`、OPL4 の
    `PCM`）は `[MEM]` の行に出る
  - ROM ファイルを 1 つ置かない：`not found` を出し、そのメモリには
    `FmEngine_SetMemory` を呼ばない
  - 外部メモリの関数をエクスポートしない変種：断りを 1 行出し、
    `FmEngine_SetMemory` を呼ばない
  - 番号で指定する形の変種（変更前のヘッダでビルドし、呼ばれたら記録する）：
    断りを 1 行出し、記録は出ない
  - `FmEngine_GetMemoryName` をエクスポートしない変種：ロードを失敗にする
  - `FmEngine_GetMemoryName` が NULL を返す変種：`[MEM]` の行で知らせ、
    `FmEngine_SetMemory` を呼ばない
  - 5 つの変種は、ヘッダをエンジンの側（`FMENGINE_EXPORTS`）から使って、
    `/W4 /WX` でビルドできる
- 対応前の DLL（YMFMEngine.dll と DSAemuEngine.dll）をロードでき、
  `patches/all.json` の WAV は、次の節で書き出したものとバイト一致する。ROM
  ファイルを置かずに比べたので、ROM の有無による違いは見ていない
- ヘッダは MSVC の C（`/TC`）と C++（`/TP`）の両方で通り、20 関数すべてを、
  仕様書の引数を書いた関数ポインタに代入できる。`FmMemoryType`、
  `FmEngine_GetMemorySize`、`FmPart`、`FmEngine_GetPartMask` を使うコードは
  コンパイルできない。変更前のヘッダでは、同じ代入の試験が落ちる（対照）

**未検証**：

- 実際のエンジンでの動作。検証用のエンジンは音を出さないので、渡した ROM が音に
  反映されることは確かめていない
- 対応前のエンジンに ROM ファイルを置いた場合の音。ROM が渡らないことは上の
  変種で確かめたが、変更前の exe と鳴らして比べてはいない
- GCC / Clang でのビルド（手元に無い）
- 仕様書の C# サンプル（コンパイルしていない）

## 部位を名前で指定する／ヘッダの正本をこのリポジトリに置く

利用者の要望：

- 部位ゲインの API を使いやすくしたい。呼び出し側が定数（`FmPart`）を知らなければ
  ならず、部位を持つデバイスが増えるたびに定数を足さなければならない
- ヘッダの正本をこのリポジトリで管理したい

仕様は `docs/FmEngineApi.md` の「部位ごとのゲイン (任意)」、ヘッダは
`include/FmEngineApi.h`。仕様書とヘッダを実装より先に書いた。新しい形の部位ゲインを
実装したエンジンは、まだ無い。

### 変更前

**確認済み**（2026-10-03 に各リポジトリを fetch し、`origin/HEAD` を `git grep` で
検索した。YMEngine `7d8ed2d`、NukedEngine `933f16d`、FMgenEngine `8a87d40`、
DSAemuEngine `815c42a`、DBOPLEngine `c67d84f`、EPSGemuEngine `7cd1e8a`、
DSGemuEngine `a1894f5`、SAASoundEngine `ca97218`、SCCIBridgeEngine `808065e`、
NesSndEngine `51ba9a6`、Y8960emu `da2ab34`、FitomEmuIF `75d9542`、FITOM_X
`29b2c31`、FITOMApp `dbb7c25`、Y8960Sequencer `a3df3f8`）：

- 番号で指定する部位ゲイン（`FmPart`、`FmEngine_GetPartMask`）をエクスポートする
  エンジンは 7 本。実装が参照する定数は、YMEngine が 9 個すべて、NukedEngine が
  OPLL・OPL3・OPL4 の 7 個、FMgenEngine が OPN の 2 個、DSAemuEngine が OPLL の
  2 個、DBOPLEngine が OPL3 の 2 個。EPSGemuEngine と DSGemuEngine は、マスクが
  常に 0 のスタブ
- SAASoundEngine、SCCIBridgeEngine、NesSndEngine、Y8960emu はエクスポートしない
- 部位ゲインを呼び出すのは、エンジン自身のテストだけ（YMEngine 24 行、
  FMgenEngine 15 行、DSAemuEngine 3 行、DBOPLEngine 3 行）。アプリケーション側の
  FitomEmuIF、FITOM_X、FITOMApp、Y8960Sequencer には呼び出しが無い
- 部位ゲインが入ったのは YMEngine が 2026-10-01（`623c811`）、ほかの 6 本が
  10-02。7 本ともタグは無い
- `FmEngineApi.h` という名前のヘッダは、他のリポジトリに 8 本あり、内容は 5 通り
  （改行の違いを除く）。ほかに、名前を変えた派生が 3 本ある（FMgenEngine の
  `FmGenEngine.h`、NukedEngine の `NukedEngineApi.h`、SCCIBridgeEngine の
  `ScciFmEngine.h`）
- このリポジトリの `src/FmEngineApi.h` は初版（`c3f0e43`）のままで、部位ゲインも
  `FmEngine_SetMemoryEx` も載っていなかった。`src/main.cpp` はこれを include せず、
  型を再定義していた

上に挙げたリポジトリの外で部位ゲインを呼び出している利用者がいるかは**未確認**。

前の節「FmEngineApi 仕様書を YMEngine の部位ゲイン追加に合わせる」の前提
（YMEngine 以外の互換エンジンが部位ゲインを実装していない）は、上のとおり
成り立たなくなっている。任意のエクスポートとする決定は変えていない。

### 決めたこと（利用者と決めた）

- 部位は名前の文字列で指定する。チップが持つ部位は `FmEngine_GetPartCount` /
  `FmEngine_GetPartName` で列挙する。チップの指定（名前で指定し、一覧は
  問い合わせる）と同じ流儀にした
- 関数の名前は変えずに置き換える。`FmEngine_SetPartGain` / `FmEngine_GetPartGain`
  の第 3 引数を `FmPart` から `const char*` にし、`FmPart` と
  `FmEngine_GetPartMask` は無くす
- 部位の名前は、チップの中で一意な短い名前にする。OPN 系は `FM` / `SSG`、OPLL 系は
  `MELODY` / `RHYTHM`、OPL3 は `AB` / `CD`、OPL4 は `DO0` / `DO1` / `DO2`。
  大文字小文字を区別する
- ヘッダの正本は `include/FmEngineApi.h` に置く

前提：

- 番号で指定する形を呼び出すアプリケーションが無いこと（上の確認の範囲）。
  古いヘッダでビルドした呼び出し側が新しい DLL の `FmEngine_SetPartGain` を
  呼ぶと、DLL は番号をポインタとして読む
- チップあたりの部位が数個で、名前の比較の負荷が問題にならないこと
- 各エンジンへのヘッダの配り方は、今までどおり写しであること。写し元が YMEngine
  からこのリポジトリに変わる

やり直しの値段：

- 関数や部位の名前を変える：仕様書の 1 節とヘッダ、それに追随を済ませたエンジン。
  アプリケーションが使い始めた後は、アプリケーションと、部位の名前を書いた
  設定ファイルにも及ぶ
- ヘッダの場所を変える：写し元として書いた各エンジンの文書に及ぶ

### 仕様書・ヘッダに書いたが、利用者と明示的には決めていないこと

変えるときは、仕様書の該当行とヘッダのコメントで済む（実装しているエンジンが
まだ無いため）。

- `FmEngine_GetPartCount` は、未知の chip_id に 0 を返す（部位を持たないチップと
  区別しない）。`FmEngine_GetPartName` は、範囲外と未知の chip_id に NULL を返す
- 4 関数は組でエクスポートする。呼び出し側は `FmEngine_GetPartCount` の有無で
  判定する。番号で指定する形の DLL も `FmEngine_SetPartGain` をエクスポートして
  いるので、その有無では見分けられない
- 名前の文字列は `FmEngine_Destroy` が戻るまで有効
- `part` が NULL なら `FM_ERR_INVALID_ARG`
- `index` の順序は定めない。同じ chip_id には同じ順序で同じ名前を返す
- 名前は ASCII の英大文字・数字・`_` で付ける
- 仕様書の表にあるチップに部位を持たせるエンジンは、表の名前と既定値を使う。
  表に無いチップの部位は、名前と既定値をエンジンが決める
- ヘッダは `<stdint.h>` を include する。変更前は `<cstdint>` で、C からは
  使えなかった
- ヘッダのコメントは特定のエンジンに依らない書き方にした。YMEngine のヘッダに
  あった YMEngine 固有の記述（`FmEngine_SetMemory` が `data` を必ず参照する、
  KEY ON/OFF の衝突で書き込みの反映を持ち越す、など）は載せていない
- `src/main.cpp` は型の再定義をやめ、ヘッダを include して関数ポインタの型を取る
- `cmake/CheckApiSymbols.cmake` を足した。ヘッダが宣言する関数と、仕様書の
  「エクスポートシンボル一覧」が食い違うと configure が止まる。比べるのは
  名前だけ

### 見送った案

- チップごとに 0 から振った番号で指定し、名前は表示用に引く。理由：番号を
  直書きでき、エンジンによって並びが違うと、黙って別の出力のゲインが変わる。
  防ぐには仕様書で並びを固定することになり、番号の表が残る
- 今の番号を残し、番号から名前を引く関数だけを足す。理由：新しいデバイスに
  番号を割り当てる手間と、32 部位の上限が残る
- 新しい関数に別の名前を付け、番号で指定する 3 関数を仕様から外す、または
  非推奨として残す。理由：利用者が同じ名前での置き換えを選んだ。非推奨として
  残す場合は `FmPart` の定数もヘッダに残る
- 部位の名前に今の定数名の接尾辞（`OPN_FM`、`OPL3_AB` など）を使う。理由：
  チップを指定した上で渡すので、チップ名を繰り返す必要が無い。OPNA の部位が
  `OPN_FM` になるなど、チップ名と食い違って見える
- ヘッダを `src/FmEngineApi.h` に置いたままにする。理由：利用者が `include/` を
  選んだ
- テストツールで部位ゲインを扱う（パッチ JSON に部位ゲインのキーを足す、部位を
  一覧表示する）。理由：今回の要望に無い。パッチのキーは外に出る値なので、
  足すときに決める

### エンジン側の対応（まだ）

番号で指定する形をエクスポートしている 7 本は、次を直すと準拠する。

- ヘッダを `include/FmEngineApi.h` の写しにする（名前を変えた派生ヘッダは、
  同じ宣言に直す）
- `FmEngine_GetPartMask` をやめ、`FmEngine_GetPartCount` /
  `FmEngine_GetPartName` を足す。`.def` を持つエンジンは `.def` も直す
- `FmEngine_SetPartGain` / `FmEngine_GetPartGain` は、部位の名前を受け取る
- 部位を持たない EPSGemuEngine と DSGemuEngine は、0 を返すスタブにするか、
  4 関数のエクスポートをやめる（どちらも準拠）

ほかのエンジンとアプリケーションは、ヘッダの写しを差し替える。

対応前のエンジンは `FmEngine_GetPartCount` を持たないので、新しい呼び出し側からは
部位を持たないエンジンに見える。

### 確認

**確認済み**：

- `cmake/CheckApiSymbols.cmake`：今のヘッダと仕様書で通る（19 シンボル）。写しを
  4 通りに崩すと、どれも落ちる（仕様書の一覧から 1 つ抜く、一覧に
  `FmEngine_GetPartMask` を残す、ヘッダの宣言を改名してコメントにだけ元の名前を
  残す、節の見出しを変える）
- `src/main.cpp` を MSVC 19.44（x64、Release）でビルドできる。エンジンの
  インポートライブラリ無しでリンクが通る
- 変更前（`866f4a3`）と変更後の exe で `patches/all.json` を WAV に書き出すと、
  バイト一致する。YMFMEngine.dll（16 チップ、183 秒）と DSAemuEngine.dll
  （102 秒）で比べた。どちらも無音ではない。DLL は手元のビルドにあったもので、
  どのコミットからビルドされたかは確かめていない
- ヘッダは MSVC の C（`/TC`）と C++（`/TP`）の両方で、`/W4 /WX` で通る。任意の
  5 関数は、仕様書の引数を書いた関数ポインタに代入して確かめた。変更前の
  ヘッダは C では通らない（対照）

**未検証**：

- GCC / Clang でのビルド（手元に無い）
- `FmEngine_SetMemory` の呼び出し。ROM ファイルが手元に無く、上の書き出しでは
  通っていない。宣言の引数は、変更前の再定義と読んで比べて同じ
- 仕様書の C# サンプル（コンパイルしていない）

## FmEngine_AddChip の clock=0（標準クロック）を廃止する

利用者の指摘：標準クロックは典型的な値にすぎない。特定の値に決める根拠は「実機の
実装例がある」ことだけで、ただ一つに決める理由が無い。

### 変更前

**確認済み**（ソースを読んだ。YMEngine `ac29207`、NukedEngine `9ae2207`、
FMgenEngine `3ba7f6d`、DSAemuEngine `815c42a`、SAASoundEngine `4173159`、
DSGemuEngine `ab98e87`、EPSGemuEngine `63c8834`）：

- 7 本とも、clock=0 なら自前の既定値を使う
- 既定値がエンジンによって食い違う。SSG は FMgen と Nuked（チップ名 `PSG`）が
  3,579,545、DSAemu と EPSGemu が 2,000,000。同じ `ssg.json` でも、エンジンに
  よって前提のクロックが違っていた
- `SSGS` は DSAemu では Y8960 の SSGS（3,579,545）、EPSGemu では YMZ705
  （4,096,000）。同じチップ名で別のチップを指している。この変更では扱わない
- FMEngineTest の `src/main.cpp` は常に clock=0 を渡していた

DBOPLEngine と SCCIBridgeEngine は**未確認**（ソースが手元に無い）。

### 決めたこと（利用者と決めた）

- `FmEngine_AddChip` は clock=0 を受け付けない。エンジンは `FM_ERR_INVALID_ARG`
  を返す。エンジンは既定のクロックを持たない
- FMEngineTest は、パッチ JSON のチップ定義の `clock`（必須）を渡す。無い・0・
  正の整数でない場合は、そのチップをスキップする

前提：レジスタ値（F-Number、トーン周期など）を書く側が、そのクロックを知っている
こと。パッチは特定のクロックを前提に書かれているので、同じファイルに置く。

やり直しの値段：仕様書は `AddChip` の節だけ。エンジンは各リポジトリで数行。
パッチは 21 ファイルに `clock` がある。

見送った案：

- 呼び出し側に 0 を禁じるだけにし、エンジンの挙動は定めない。理由：0 を渡す
  呼び出し側が今までどおり動いてしまい、気づかれない
- パッチの `clock` を任意にし、省略時は FMEngineTest の表で補う。理由：ツールの
  中に標準クロックを作り直すことになる

エンジン側の対応はまだ。上の 7 本は、0 を既定値に読み替えるので仕様に準拠しない。
FMEngineTest は 0 を渡さなくなったので、対応前のエンジンでもそのまま動く。

### パッチに書いた値

チップを実装するエンジンの既定値がすべて一致するものは、その値にした。今までの
出力が変わらない。

| チップ | clock |
|---|---|
| Y8950, OPL, OPL2, OPLL, OPLLP, OPLLX, VRC7, OPM, OPZ | 3,579,545 |
| OPL3 | 14,318,180 |
| OPL4 | 33,868,800 |
| OPN | 3,993,600 |
| OPNA | 7,987,200 |
| OPNB, OPNBB | 8,000,000 |
| OPN2 | 7,670,453 |
| DCSG, SCC | 3,579,545（DSAemu だけが実装） |
| SAA | 8,000,000（SAASound だけが実装） |
| SSG | 3,579,545 |

SSG は食い違っていたので、パッチを書いた時点の値にした。パッチは YMEngine の
リポジトリで作られ、当時の YMEngine の SSG は 3,579,545 だった（YMEngine
`130c5e8^` の `src/ExternalChip.h`）。**推測**：パッチのトーン周期（106、129、
154）はどのクロックでも音階にならず、レジスタ値からは決められなかった。DSAemu と
EPSGemu で鳴らすと、SSG の音程が今までと変わる。

`docs/patch-format.md` の最小構成の例は、コメントの 261Hz とレジスタ値が合って
いなかった（fnum 0x241、block 4 は 3,579,545 Hz で約 438 Hz）。`opl2.json` の
CH0 の値（fnum 0x2B0、block 3、約 261.0 Hz）に差し替えた。

### 確認

**確認済み**（`src/main.cpp` を MSVC 19.29 でビルドし、手元のビルドにあった DLL で
WAV を書き出した。DLL がどのコミットからビルドされたかは確かめていない）：

- `clock` が無い・0・-1 のチップは `[SKIP]` になる。7,159,090 にすると OPL2 の
  native_rate が 99,431 になる（3,579,545 では 49,715）。clock がエンジンに
  渡っている
- `all.json` から `$ref` で読んだチップにも `clock` が付く（YMEngine の 16 チップが
  追加された）
- 変更前の exe と変更前のパッチ（clock=0）、変更後の exe と変更後のパッチで、
  `all.json` と `test_patches.json` を 6 つの DLL で書き出して比べた。YMFMEngine、
  NukedEngineApi、DBOPLEngine、SAASoundEngine、FmEngineApi はどちらもバイト一致。
  FmGenEngineApi は `test_patches.json`（OPNA、OPNB、OPNBB、OPN2、OPM、SSG）が
  バイト一致し、`all.json` は不一致だった
- FmGenEngineApi は、同じ exe・同じパッチでも実行ごとに出力が変わる（OPN、OPNA、
  SSG で観測。変更前の exe でも起きる）。チップごとに書き出し、食い違った区間の
  音程をゼロ交差で比べると新旧で一致した（SSG は 4,220.7 Hz 同士）。ただし FM の
  区間はゼロ交差の揺れが ±0.5% あり、近いクロックの違いは見分けられない。FMgen の
  OPN の値の根拠はソースの既定値

DSAemuEngine、DSGemuEngine、EPSGemuEngine は DLL が手元に無く、書き出していない
（**未検証**）。値の根拠はソースの既定値。

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

**確認済み**（ソースを読んだ。上と同じコミット）：手元のエンジンは `SetMemory` の
`data` に書き込まない。YMEngine は `writeable=false` で書き込みを捨てる。FMgen の
OPNA と DSAemu の Y8950 は複製に書く。FMgen の OPNB/OPNBB は `data` を fmgen に
直接渡すが、ADPCM-A のバッファに書く処理は無く、ADPCM-B は control1 を `& 0x91` で
マスクし、データレジスタを `SetADPCMBReg` に回さないので `WriteRAM` に届かない。

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
- 既存の `SetMemory` の `data` には、エンジンは書き込まない。`const` ポインタで
  受け取るので、呼び出し側は読み取り専用のメモリを渡しうる。複製するか、複製した
  場合にチップの書き込みを複製に反映するかは、エンジンのコア実装に任せる
- `SetMemoryEx` は、外部メモリのバスが外に出ているチップを扱うエンジンには
  エクスポートを勧める。外部回路でホストや他のデバイスとメモリを共有する構成が
  ありうるため。バスが外に出ていないメモリ（OPNA のリズム用メモリなど）だけなら
  この限りではない
- 「外部メモリ」は音源コアの外部という意味で、物理的にチップの外とは限らない
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
