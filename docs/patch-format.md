# パッチ / テストスイート JSON フォーマット仕様

`FMEngineTest` が読み込む JSON ファイルには **パッチファイル** と **テストスイートファイル** の 2 種類があります。  
`test_suite` キーを持つ JSON がテストスイートとして扱われ、それ以外はパッチファイルです。

---

## パッチファイル

チップへのレジスタ操作列を記述します。

### 最小構成の例

```json
{
  "global": { "note_ms": 800, "rest_ms": 200 },
  "chips": {
    "OPL2": {
      "gain": 1.0,
      "init": [ {"reg": "0x01", "val": "0x20"} ],
      "channels": [
        {
          "ch": 0,
          "_comment": "261Hz",
          "init":    [ {"reg": "0xA0", "val": "0x41"} ],
          "key_on":  [ {"reg": "0xB0", "val": "0x32"} ],
          "key_off": [ {"reg": "0xB0", "val": "0x12"} ]
        }
      ]
    }
  }
}
```

### トップレベルフィールド

| フィールド | 型 | 説明 |
|---|---|---|
| `sample_rate` | number | エンジンのサンプルレート (Hz)。現在は参照のみで、実際には CLI `-r` または テストスイートの値が使われる |
| `global.note_ms` | number | デフォルト発音時間 (ms)。省略時 800 |
| `global.rest_ms` | number | デフォルト休符時間 (ms)。省略時 200 |
| `chips` | object | チップ定義マップ。キーはチップ名 |

### chips フィールド

チップ名は `FmEngine_GetSupportedChip` で列挙される文字列と一致させてください（大文字小文字を区別）。

| フィールド | 型 | 説明 |
|---|---|---|
| `gain` | number | L/R 共通ゲイン。省略時 1.0 |
| `gain_l` / `gain_r` | number | 左右独立ゲイン。指定時は `gain` より優先 |
| `init` | array | 初期化レジスタ列。チップ起動時に 1 回だけ書き込む |
| `channels` | array | チャンネル定義リスト |
| `"$ref"` | string | 同名チップの定義を参照先ファイルから読み込む |

### channels 要素フィールド

| フィールド | 型 | 説明 |
|---|---|---|
| `ch` | number | チャンネル番号 (表示用) |
| `port` | number | デフォルトポート番号。省略時 0 |
| `note_ms` | number | 発音時間 (ms)。省略時 `global.note_ms` |
| `rest_ms` | number | 休符時間 (ms)。省略時 `global.rest_ms` |
| `_comment` | string | コメント文字列 (表示用) |
| `init` | array | チャンネル初期化レジスタ列 |
| `key_on` | array | KEY ON レジスタ列 |
| `key_off` | array | KEY OFF レジスタ列 |

### レジスタ記述形式

```json
{ "reg": "0xA4", "val": "0x11", "port": 0 }
```

| フィールド | 型 | 説明 |
|---|---|---|
| `reg` | string | レジスタアドレス。10 進・16 進どちらも可 |
| `val` | string | 書き込む値。10 進・16 進どちらも可 |
| `port` | number | ポート番号。省略時はチャンネルの `port` 値 |

### $ref による外部参照

チップ定義に `"$ref": "other.json"` を指定すると、  
`other.json` 内の同名チップ定義を読み込んで置換します。  
`all.json` のように複数チップを束ねるファイルを作る際に使います。

```json
{
  "chips": {
    "OPNA": { "$ref": "opna.json" },
    "OPL2": { "$ref": "opl2.json" }
  }
}
```

循環参照は検出して無視されます（最大深さ 8）。

---

## テストスイートファイル

`test_suite` キーを持つ JSON です。  
複数のエンジン・サンプルレート・パッチの組み合わせを 1 ファイルで定義し、順番に自動実行できます。

### 構成例

```json
{
  "_comment": "テストスイートの例",
  "test_suite": [
    {
      "engine":      "YMEngine.dll",
      "sample_rate": 48000,
      "device":      "Realtek",
      "patches":     ["patches/opna.json", "patches/opm.json"]
    },
    {
      "engine":      "NukedEngine.dll",
      "sample_rate": 44100,
      "patches":     ["patches/opl2.json", "patches/opl3.json"]
    },
    {
      "engine":      "YMEngine.dll",
      "sample_rate": 48000,
      "output_wav":  "out/ymengine_opna.wav",
      "patches":     ["patches/opna.json"]
    },
    {
      "_comment": "engine/sample_rate/device を省略すると CLI オプションの値が使われる",
      "patches":  ["patches/all.json"]
    }
  ]
}
```

### test_suite エントリフィールド

| フィールド | 型 | 省略時の挙動 |
|---|---|---|
| `engine` | string | CLI `-e` の値、またはデフォルト DLL |
| `sample_rate` | number | CLI `-r` の値、またはデフォルト 48000 |
| `device` | string | CLI `-d` の値、またはデフォルトデバイス |
| `patches` | string[] | **必須**。省略するとエントリがスキップされる |
| `output_wav` | string | WAV ファイルの出力パス。省略するとリアルタイム再生のみ |

### output_wav — WAV エクスポート動作

`output_wav` を指定したエントリはリアルタイム再生ではなく、オフラインレンダリングで WAV ファイルを生成します。

- フォーマット: 16-bit PCM、ステレオ、`sample_rate` で指定したレート
- 出力先ディレクトリが存在しない場合は自動作成されます
- レンダリング長は `patches` 内のすべてのチャンネルの `note_ms + rest_ms` 合計に基づきます
- `output_wav` がないエントリは従来通りリアルタイム再生します

### CLI オプションとの優先順位

```
各フィールドの優先順位:  test_suite エントリ内の値  >  CLI オプション  >  デフォルト値
```

同じファイルにパッチファイルとテストスイートファイルを混在させることもできます。  
パッチファイルは先頭の 1 エントリにまとめて実行されます。
