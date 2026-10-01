# CLAUDE.md

AI 向けの作業メモ。人間向けの文書は `README.md`、`docs/FmEngineApi.md`、
`docs/patch-format.md`。

## 文書の置き場所

- `docs/CHANGELOG.md` — 開発経緯。方針の前提、見送った案、確認結果を書く
- `docs/FmEngineApi.md` — FmEngineApi の仕様（互換エンジンすべてが従う側）。
  参照実装は YMEngine の `src/FmEngineApi.h` / `src/FmEngineApi.def`。
  YMEngine の API が変わったら、この仕様書と `src/main.cpp` の型の再定義を
  見比べる
