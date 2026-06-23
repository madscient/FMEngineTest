// main.cpp
// FmEngineApi 準拠エンジンの汎用テストツール (FMEngineTest)
//
// 使い方:
//   FMEngineTest [オプション] [file1.json] [file2.json] ...
//
//   引数なし         : patches/all.json を使用
//   JSON ファイル指定 : 指定したファイルを順に処理 (複数指定可)
//   -e <dllpath>     : エンジン DLL のパスを指定
//                      省略時: FmEngineApi.dll (Windows) / libFmEngineApi.so (Linux/macOS)
//   -r <rate>        : サンプルレートを指定 (省略時 48000)
//   -d <name>        : デバイス名を部分一致で指定 (省略時デフォルトデバイス)
//
// パッチ JSON フォーマット:
//   {
//     "sample_rate": 48000,
//     "global": { "note_ms": 800, "rest_ms": 200 },
//     "chips": {
//       "OPL2": { "gain": 1.0, "init": [...], "channels": [...] },
//       "OPNA": { "gain_l": 0.8, "gain_r": 1.0, "init": [...], "channels": [...] },
//       "OPM":  { "$ref": "opm.json" }
//     }
//   }
//
//   gain        : L/R 共通ゲイン (省略時 1.0)
//   gain_l/gain_r: 左右個別ゲイン (指定時 "gain" より優先)
//   "$ref"      : 他の JSON ファイル内の同名チップ定義を参照する
//
// テストスイート JSON フォーマット:
//   {
//     "test_suite": [
//       {
//         "engine":      "YMEngine.dll",       // エンジン DLL パス (省略時 CLI -e または デフォルト)
//         "sample_rate": 48000,                 // サンプルレート   (省略時 CLI -r または 48000)
//         "device":      "Realtek",             // デバイス名部分一致 (省略時 CLI -d またはデフォルト)
//         "patches":     ["patches/opna.json", "patches/opl2.json"],  // パッチファイルリスト
//         "output_wav":  "out/result.wav"   // WAV出力パス (省略時はリアルタイム再生のみ)
//       },
//       { ... }
//     ]
//   }
//
//   test_suite キーを持つ JSON ファイルはテストスイートとして扱われる。
//   各エントリのフィールドは省略可能で、省略時は CLI オプションの値が使われる。
//   エントリごとにエンジン DLL のロード/アンロードが行われる。

#include "RtAudio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#  include <time.h>
#  include <sys/stat.h>
#  include <errno.h>
#endif
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// =========================================================
//  FmEngineApi 型定義
//  (FmEngineApi.h を include せず、ここで再定義する)
// =========================================================
typedef struct FmEngineOpaque* FmEngineHandle;

typedef enum {
    FM_OK                =  0,
    FM_ERR_INVALID_ARG   = -1,
    FM_ERR_UNKNOWN_CHIP  = -2,
    FM_ERR_ALLOC         = -3,
    FM_ERR_UNAVAILABLE   = -4,
} FmResult;

typedef enum {
    FM_MEM_ADPCM_A = 1,
    FM_MEM_ADPCM_B = 2,
    FM_MEM_PCM     = 3,
} FmMemoryType;

// =========================================================
//  関数ポインタ型定義
// =========================================================
typedef FmEngineHandle (*PFN_FmEngine_Create)(uint32_t);
typedef void           (*PFN_FmEngine_Destroy)(FmEngineHandle);
typedef uint32_t       (*PFN_FmEngine_Inquiry)(FmEngineHandle);
typedef const char*    (*PFN_FmEngine_GetSupportedChip)(FmEngineHandle, uint32_t);
typedef FmResult       (*PFN_FmEngine_AddChip)(FmEngineHandle, const char*, uint32_t, uint32_t*);
typedef const char*    (*PFN_FmEngine_GetChipName)(FmEngineHandle, uint32_t);
typedef uint32_t       (*PFN_FmEngine_GetNativeRate)(FmEngineHandle, uint32_t);
typedef uint32_t       (*PFN_FmEngine_GetSampleRate)(FmEngineHandle);
typedef FmResult       (*PFN_FmEngine_Write)(FmEngineHandle, uint32_t, uint8_t, uint8_t, uint32_t);
typedef FmResult       (*PFN_FmEngine_SetGain)(FmEngineHandle, uint32_t, float, float);
typedef FmResult       (*PFN_FmEngine_GetGain)(FmEngineHandle, uint32_t, float*, float*);
typedef FmResult       (*PFN_FmEngine_SetMemory)(FmEngineHandle, uint32_t, FmMemoryType, const uint8_t*, uint32_t);
typedef uint32_t       (*PFN_FmEngine_GetMemorySize)(FmEngineHandle, uint32_t, FmMemoryType);
typedef FmResult       (*PFN_FmEngine_Generate)(FmEngineHandle, float*, float*, uint32_t);

// =========================================================
//  関数ポインタを束ねた構造体
// =========================================================
struct FmEngineApi {
    PFN_FmEngine_Create          Create          = nullptr;
    PFN_FmEngine_Destroy         Destroy         = nullptr;
    PFN_FmEngine_Inquiry         Inquiry         = nullptr;
    PFN_FmEngine_GetSupportedChip GetSupportedChip= nullptr;
    PFN_FmEngine_AddChip         AddChip         = nullptr;
    PFN_FmEngine_GetChipName     GetChipName     = nullptr;
    PFN_FmEngine_GetNativeRate   GetNativeRate   = nullptr;
    PFN_FmEngine_GetSampleRate   GetSampleRate   = nullptr;
    PFN_FmEngine_Write           Write           = nullptr;
    PFN_FmEngine_SetGain         SetGain         = nullptr;
    PFN_FmEngine_GetGain         GetGain         = nullptr;
    PFN_FmEngine_SetMemory       SetMemory       = nullptr;
    PFN_FmEngine_GetMemorySize   GetMemorySize   = nullptr;
    PFN_FmEngine_Generate        Generate        = nullptr;
};

// =========================================================
//  DLL ローダー
// =========================================================
#ifdef _WIN32
using DllHandle = HMODULE;
static DllHandle dllOpen(const char* path)  { return LoadLibraryA(path); }
static void      dllClose(DllHandle h)      { FreeLibrary(h); }
static void*     dllSym(DllHandle h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(h, name));
}
static std::string dllError() {
    DWORD e = GetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, e, 0, buf, sizeof(buf), nullptr);
    return buf;
}
#else
using DllHandle = void*;
static DllHandle dllOpen(const char* path)  { return dlopen(path, RTLD_LAZY); }
static void      dllClose(DllHandle h)      { dlclose(h); }
static void*     dllSym(DllHandle h, const char* name) { return dlsym(h, name); }
static std::string dllError() { const char* e = dlerror(); return e ? e : "unknown"; }
#endif

#define LOAD_SYM(api, h, name) \
    api.name = reinterpret_cast<PFN_FmEngine_##name>(dllSym(h, "FmEngine_" #name)); \
    if (!api.name) { \
        fprintf(stderr, "FmEngine_%s not found in DLL\n", #name); \
        return false; \
    }

static bool loadApi(const char* dllPath, FmEngineApi& api, DllHandle& outHandle) {
    DllHandle h = dllOpen(dllPath);
    if (!h) {
        fprintf(stderr, "Cannot load DLL '%s': %s\n", dllPath, dllError().c_str());
        return false;
    }
    LOAD_SYM(api, h, Create)
    LOAD_SYM(api, h, Destroy)
    LOAD_SYM(api, h, Inquiry)
    LOAD_SYM(api, h, GetSupportedChip)
    LOAD_SYM(api, h, AddChip)
    LOAD_SYM(api, h, GetChipName)
    LOAD_SYM(api, h, GetNativeRate)
    LOAD_SYM(api, h, GetSampleRate)
    LOAD_SYM(api, h, Write)
    LOAD_SYM(api, h, SetGain)
    LOAD_SYM(api, h, GetGain)
    LOAD_SYM(api, h, SetMemory)
    LOAD_SYM(api, h, GetMemorySize)
    LOAD_SYM(api, h, Generate)
    outHandle = h;
    return true;
}

// =========================================================
//  RtAudio コールバック
// =========================================================
struct AudioState {
    FmEngineHandle  eng = nullptr;
    const FmEngineApi* api = nullptr;
    std::vector<float> workL;
    std::vector<float> workR;
};

static int rtAudioCallback(void* outBuf, void* /*inBuf*/,
                            unsigned int nFrames,
                            double /*streamTime*/,
                            RtAudioStreamStatus /*status*/,
                            void* userData) {
    auto* st = static_cast<AudioState*>(userData);
    if (!st->eng || !st->api) return 0;
    if (st->workL.size() < nFrames) st->workL.resize(nFrames, 0.0f);
    if (st->workR.size() < nFrames) st->workR.resize(nFrames, 0.0f);
    st->api->Generate(st->eng, st->workL.data(), st->workR.data(), nFrames);
    auto* dst = static_cast<float*>(outBuf);
    for (unsigned int i = 0; i < nFrames; ++i) {
        dst[i * 2    ] = st->workL[i];
        dst[i * 2 + 1] = st->workR[i];
    }
    return 0;
}

// =========================================================
//  ヘルパー
// =========================================================
static void check(FmResult r, const char* msg) {
    if (r != FM_OK) {
        fprintf(stderr, "ERROR %s: code=%d\n", msg, (int)r);
#ifdef _WIN32
        ExitProcess(1);
#else
        exit(1);
#endif
    }
}

static void sleepMs(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts{ static_cast<time_t>(ms / 1000),
                        static_cast<long>((ms % 1000) * 1000000L) };
    nanosleep(&ts, nullptr);
#endif
}

static std::string getExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
#elif defined(__linux__)
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) len = 0;
    buf[len] = '\0';
    std::string path(buf);
#else
    std::string path;
#endif
    const auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

static std::vector<uint8_t> loadRomFile(const std::string& filename) {
    const std::string path = getExeDir() + filename;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return {};
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return {}; }
    std::vector<uint8_t> buf(sz);
    fread(buf.data(), 1, sz, fp);
    fclose(fp);
    return buf;
}

static uint32_t parseVal(const std::string& s) {
    return (uint32_t)std::stoul(s, nullptr, 0);
}

// =========================================================
//  ROM テーブル
// =========================================================
struct RomEntry {
    std::string chipName;
    FmMemoryType memType;
    std::string filename;
    std::string description;
};

static const RomEntry kRomTable[] = {
    { "OPNA",  FM_MEM_ADPCM_A, "ym2608.rom",  "YM2608 ADPCM-A ROM" },
    { "OPNB",  FM_MEM_ADPCM_A, "ym2610.rom",  "YM2610 ADPCM-A ROM" },
    { "OPNB",  FM_MEM_ADPCM_B, "ym2610b.rom", "YM2610 ADPCM-B ROM" },
    { "OPNBB", FM_MEM_ADPCM_A, "ym2610.rom",  "YM2610 ADPCM-A ROM" },
    { "OPNBB", FM_MEM_ADPCM_B, "ym2610b.rom", "YM2610 ADPCM-B ROM" },
};

// =========================================================
//  $ref 解決
// =========================================================
static bool loadJson(const char* path, json& out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "Cannot open: %s\n", path); return false; }
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp); rewind(fp);
    std::string buf(sz, '\0');
    fread(buf.data(), 1, sz, fp);
    fclose(fp);
    try   { out = json::parse(buf); return true; }
    catch (const std::exception& e) {
        fprintf(stderr, "JSON parse error (%s): %s\n", path, e.what());
        return false;
    }
}

static std::string resolveRefPath(const std::string& basePath, const std::string& refPath) {
    const auto pos = basePath.find_last_of("\\/");
    if (pos == std::string::npos) return refPath;
    return basePath.substr(0, pos + 1) + refPath;
}

static void resolveChipRefs(json& chips, const std::string& currentPath,
                             std::vector<std::string>& visitedPaths,
                             int depth = 0) {
    if (depth > 8) return;
    for (auto it = chips.begin(); it != chips.end(); ++it) {
        const std::string chipName = it.key();
        json& chipDef = it.value();
        if (!chipDef.is_object() || !chipDef.contains("$ref")) continue;
        const std::string refFullPath =
            resolveRefPath(currentPath, chipDef["$ref"].get<std::string>());
        bool cyclic = false;
        for (const auto& p : visitedPaths) if (p == refFullPath) { cyclic = true; break; }
        if (cyclic) { chipDef = json::object(); continue; }
        json refRoot;
        if (!loadJson(refFullPath.c_str(), refRoot) ||
            !refRoot.contains("chips") || !refRoot["chips"].contains(chipName)) {
            chipDef = json::object(); continue;
        }
        visitedPaths.push_back(refFullPath);
        resolveChipRefs(refRoot["chips"], refFullPath, visitedPaths, depth + 1);
        visitedPaths.pop_back();
        chipDef = refRoot["chips"][chipName];
    }
}

// =========================================================
//  ファイルコンテキスト
// =========================================================
struct FileContext {
    json     root;
    std::string path;
    bool     valid = false;
    struct Slot { uint32_t id = 0; bool valid = false; };
    std::vector<Slot> slots;
    std::vector<std::vector<uint8_t>> romBuffers;
};

static void applyRegs(const FmEngineApi& api, FmEngineHandle eng,
                      uint32_t chip_id, const json& regs,
                      uint32_t default_port = 0) {
    for (const auto& r : regs) {
        const uint32_t port = r.value("port", default_port);
        api.Write(eng, chip_id,
                  (uint8_t)parseVal(r["reg"].get<std::string>()),
                  (uint8_t)parseVal(r["val"].get<std::string>()),
                  port);
    }
}

static void addChipsFromFile(const FmEngineApi& api, FileContext& ctx,
                              FmEngineHandle eng) {
    if (!ctx.valid) return;
    if (!ctx.root.contains("chips") || !ctx.root["chips"].is_object()) return;

    const auto& chips = ctx.root["chips"];
    ctx.slots.reserve(chips.size());

    for (auto it = chips.begin(); it != chips.end(); ++it) {
        const std::string chipName = it.key();
        const auto& chipDef = it.value();

        uint32_t chip_id = 0;
        const FmResult res = api.AddChip(eng, chipName.c_str(), 0, &chip_id);
        if (res == FM_ERR_UNKNOWN_CHIP) {
            printf("  [SKIP] %s : unknown chip type\n", chipName.c_str());
            ctx.slots.push_back({0, false});
            continue;
        }
        if (res != FM_OK) {
            printf("  [SKIP] %s : AddChip failed (code=%d)\n", chipName.c_str(), (int)res);
            ctx.slots.push_back({0, false});
            continue;
        }

        const float baseGain = chipDef.value("gain", 1.0f);
        api.SetGain(eng, chip_id,
                    chipDef.value("gain_l", baseGain),
                    chipDef.value("gain_r", baseGain));

        for (const auto& entry : kRomTable) {
            if (entry.chipName != chipName) continue;
            ctx.romBuffers.push_back(loadRomFile(entry.filename));
            const auto& rom = ctx.romBuffers.back();
            if (rom.empty()) {
                printf("    [ROM] %s: not found (%s) -- ADPCM will be silent\n",
                       entry.description.c_str(), entry.filename.c_str());
                continue;
            }
            const FmResult rr = api.SetMemory(
                eng, chip_id, entry.memType, rom.data(), (uint32_t)rom.size());
            if (rr == FM_OK)
                printf("    [ROM] %s: loaded %zu bytes\n",
                       entry.description.c_str(), rom.size());
            else
                printf("    [ROM] %s: SetMemory failed (code=%d)\n",
                       entry.description.c_str(), (int)rr);
        }
        ctx.slots.push_back({chip_id, true});
    }
}

static void playChipsFromFile(const FmEngineApi& api, const FileContext& ctx,
                               FmEngineHandle eng) {
    if (!ctx.valid) return;
    if (!ctx.root.contains("chips") || !ctx.root["chips"].is_object()) return;

    const uint32_t global_note = ctx.root.value("note_ms",
        ctx.root.contains("global")
            ? ctx.root["global"].value("note_ms", 800u) : 800u);
    const uint32_t global_rest = ctx.root.value("rest_ms",
        ctx.root.contains("global")
            ? ctx.root["global"].value("rest_ms", 200u) : 200u);

    const auto& chips = ctx.root["chips"];
    uint32_t slotIdx = 0;
    for (auto it = chips.begin(); it != chips.end(); ++it, ++slotIdx) {
        if (slotIdx >= ctx.slots.size() || !ctx.slots[slotIdx].valid) continue;
        const uint32_t chip_id = ctx.slots[slotIdx].id;
        const auto& chipDef = it.value();

        if (chipDef.contains("init"))
            applyRegs(api, eng, chip_id, chipDef["init"]);

        printf("[%s] chip_id=%u, native_rate=%u Hz\n",
               api.GetChipName(eng, chip_id),
               chip_id, api.GetNativeRate(eng, chip_id));

        if (!chipDef.contains("channels")) continue;

        for (const auto& chDef : chipDef["channels"]) {
            const uint32_t chPort    = chDef.value("port", 0u);
            const uint32_t note_ms   = chDef.value("note_ms", global_note);
            const uint32_t rest_ms   = chDef.value("rest_ms", global_rest);

            printf("  CH%u", chDef.value("ch", 0u));
            if (chDef.contains("_comment"))
                printf(" %s", chDef["_comment"].get<std::string>().c_str());
            printf("\n");
            fflush(stdout);

            if (chDef.contains("init"))
                applyRegs(api, eng, chip_id, chDef["init"], chPort);
            if (chDef.contains("key_on"))
                applyRegs(api, eng, chip_id, chDef["key_on"], chPort);

            sleepMs(note_ms);

            if (chDef.contains("key_off"))
                applyRegs(api, eng, chip_id, chDef["key_off"], chPort);

            sleepMs(rest_ms);
        }
    }
    printf("\n");
}

// =========================================================
//  テストスイート
// =========================================================

// 1エントリ分のパラメータ (未指定は空文字/0 = CLIデフォルトを使う)
struct SuiteEntry {
    std::string              engine;       // "" → CLI -e / デフォルト DLL
    uint32_t                 sampleRate;   // 0  → CLI -r / 48000
    std::string              device;       // "" → CLI -d / デフォルトデバイス
    std::vector<std::string> patches;      // パッチファイルパスリスト
    std::string              outputWav;    // "" → リアルタイム再生のみ / 指定→ WAV 出力
};

// JSON が test_suite キーを持つかチェック
static bool isTestSuiteJson(const json& root) {
    return root.is_object() && root.contains("test_suite");
}

// test_suite JSON をパースしてエントリリストに変換
static std::vector<SuiteEntry> parseTestSuite(
    const json& root,
    const char* cliEngine,
    uint32_t    cliRate,
    const char* cliDevice)
{
    std::vector<SuiteEntry> entries;
    const auto& suite = root["test_suite"];
    if (!suite.is_array()) {
        fprintf(stderr, "test_suite must be a JSON array\n");
        return entries;
    }
    for (const auto& item : suite) {
        if (!item.is_object()) continue;
        SuiteEntry e;
        // engine: JSON優先、なければCLI、なければ空(呼び出し側でデフォルトDLL適用)
        if (item.contains("engine") && item["engine"].is_string())
            e.engine = item["engine"].get<std::string>();
        else if (cliEngine)
            e.engine = cliEngine;

        // sample_rate: JSON優先、なければCLI
        if (item.contains("sample_rate") && item["sample_rate"].is_number_unsigned())
            e.sampleRate = item["sample_rate"].get<uint32_t>();
        else
            e.sampleRate = cliRate;  // cliRate は呼び出し元でデフォルト48000済み

        // device: JSON優先、なければCLI
        if (item.contains("device") && item["device"].is_string())
            e.device = item["device"].get<std::string>();
        else if (cliDevice)
            e.device = cliDevice;

        // output_wav
        if (item.contains("output_wav") && item["output_wav"].is_string())
            e.outputWav = item["output_wav"].get<std::string>();

        // patches
        if (item.contains("patches") && item["patches"].is_array()) {
            for (const auto& p : item["patches"]) {
                if (p.is_string())
                    e.patches.push_back(p.get<std::string>());
            }
        }
        if (e.patches.empty()) {
            fprintf(stderr, "  [WARN] test_suite entry has no patches, skipping\n");
            continue;
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

// =========================================================
//  WAV ライター (16-bit PCM, ステレオ)
// =========================================================

// ディレクトリを再帰的に作成するヘルパー
static bool mkdirs(const std::string& path) {
    if (path.empty()) return true;
#ifdef _WIN32
    // バックスラッシュ・スラッシュ両対応
    std::string p = path;
    for (char& c : p) if (c == '/') c = '\\';
    if (CreateDirectoryA(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        return true;
    // 親ディレクトリを再帰的に作成
    const auto pos = p.find_last_of("\\/");
    if (pos == std::string::npos || pos == 0) return false;
    if (!mkdirs(p.substr(0, pos))) return false;
    return CreateDirectoryA(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    // mkdir -p 相当
    std::string tmp = path;
    for (size_t i = 1; i < tmp.size(); ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
            ::mkdir(tmp.c_str(), 0755);
            tmp[i] = '/';
        }
    }
    return ::mkdir(tmp.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// 16-bit リトルエンディアン書き込みヘルパー
static void writeU16LE(std::ofstream& f, uint16_t v) {
    uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    f.write(reinterpret_cast<char*>(buf), 2);
}
static void writeU32LE(std::ofstream& f, uint32_t v) {
    uint8_t buf[4] = {
        (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)
    };
    f.write(reinterpret_cast<char*>(buf), 4);
}

// WAV ヘッダーを書き込む (dataSize = PCM バイト数)
static void writeWavHeader(std::ofstream& f, uint32_t sampleRate,
                            uint16_t channels, uint32_t dataSize) {
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate      = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign    = channels * (bitsPerSample / 8);

    f.write("RIFF", 4);
    writeU32LE(f, 36 + dataSize);   // ファイルサイズ - 8
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32LE(f, 16);              // fmt チャンクサイズ
    writeU16LE(f, 1);               // PCM
    writeU16LE(f, channels);
    writeU32LE(f, sampleRate);
    writeU32LE(f, byteRate);
    writeU16LE(f, blockAlign);
    writeU16LE(f, bitsPerSample);
    f.write("data", 4);
    writeU32LE(f, dataSize);
}

// float サンプル [-1.0, 1.0] → int16_t にクランプ変換
static int16_t floatToS16(float v) {
    const int32_t s = static_cast<int32_t>(v * 32767.0f);
    if (s >  32767) return  32767;
    if (s < -32768) return -32768;
    return static_cast<int16_t>(s);
}

// =========================================================
//  パッチからの総再生時間計算
// =========================================================
// 全チャンネルの (note_ms + rest_ms) を合計して返す
static uint64_t calcTotalMs(const std::vector<FileContext>& ctxs) {
    uint64_t total = 0;
    for (const auto& ctx : ctxs) {
        if (!ctx.valid || !ctx.root.contains("chips")) continue;
        const uint32_t global_note = ctx.root.contains("global")
            ? ctx.root["global"].value("note_ms", 800u) : 800u;
        const uint32_t global_rest = ctx.root.contains("global")
            ? ctx.root["global"].value("rest_ms", 200u) : 200u;
        for (auto it = ctx.root["chips"].begin(); it != ctx.root["chips"].end(); ++it) {
            const auto& chipDef = it.value();
            if (!chipDef.contains("channels")) continue;
            for (const auto& chDef : chipDef["channels"]) {
                total += chDef.value("note_ms", global_note);
                total += chDef.value("rest_ms", global_rest);
            }
        }
    }
    return total;
}

// =========================================================
//  オフラインレンダリング → WAV 出力
// =========================================================
// パッチを順に適用しながら FmEngine_Generate を呼び出し続け、WAV に書き出す。
// リアルタイム再生と異なりオーディオデバイスを使わない。
static bool renderToWav(const FmEngineApi& api, FmEngineHandle eng,
                         std::vector<FileContext>& ctxs,
                         uint32_t sampleRate, const std::string& outPath) {
    // 出力ディレクトリ作成
    const auto dirPos = outPath.find_last_of("/\\");
    if (dirPos != std::string::npos && dirPos > 0) {
        mkdirs(outPath.substr(0, dirPos));
    }

    // ファイルオープン (ヘッダーは後で上書き)
    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        fprintf(stderr, "Cannot open WAV output: %s\n", outPath.c_str());
        return false;
    }

    // 仮ヘッダー (dataSize = 0、後で seek して上書き)
    writeWavHeader(f, sampleRate, 2, 0);
    const std::streampos dataStart = f.tellp();

    const uint32_t BLOCK = 512;
    std::vector<float> bufL(BLOCK), bufR(BLOCK);
    uint32_t totalSamples = 0;

    // --- パッチ再生ループ ---
    for (auto& ctx : ctxs) {
        if (!ctx.valid || !ctx.root.contains("chips")) continue;

        const uint32_t global_note = ctx.root.contains("global")
            ? ctx.root["global"].value("note_ms", 800u) : 800u;
        const uint32_t global_rest = ctx.root.contains("global")
            ? ctx.root["global"].value("rest_ms", 200u) : 200u;

        uint32_t slotIdx = 0;
        for (auto it = ctx.root["chips"].begin();
             it != ctx.root["chips"].end(); ++it, ++slotIdx) {
            if (slotIdx >= ctx.slots.size() || !ctx.slots[slotIdx].valid) continue;
            const uint32_t chip_id = ctx.slots[slotIdx].id;
            const auto& chipDef   = it.value();

            if (chipDef.contains("init"))
                applyRegs(api, eng, chip_id, chipDef["init"]);

            printf("[%s] chip_id=%u, native_rate=%u Hz\n",
                   api.GetChipName(eng, chip_id),
                   chip_id, api.GetNativeRate(eng, chip_id));

            if (!chipDef.contains("channels")) continue;

            for (const auto& chDef : chipDef["channels"]) {
                const uint32_t chPort  = chDef.value("port", 0u);
                const uint32_t note_ms = chDef.value("note_ms", global_note);
                const uint32_t rest_ms = chDef.value("rest_ms", global_rest);

                printf("  CH%u", chDef.value("ch", 0u));
                if (chDef.contains("_comment"))
                    printf(" %s", chDef["_comment"].get<std::string>().c_str());
                printf("\n");
                fflush(stdout);

                if (chDef.contains("init"))
                    applyRegs(api, eng, chip_id, chDef["init"], chPort);
                if (chDef.contains("key_on"))
                    applyRegs(api, eng, chip_id, chDef["key_on"], chPort);

                // note 期間レンダリング
                uint32_t remain = (uint32_t)((uint64_t)note_ms * sampleRate / 1000);
                while (remain > 0) {
                    const uint32_t n = (remain < BLOCK) ? remain : BLOCK;
                    api.Generate(eng, bufL.data(), bufR.data(), n);
                    for (uint32_t s = 0; s < n; ++s) {
                        const int16_t l = floatToS16(bufL[s]);
                        const int16_t r = floatToS16(bufR[s]);
                        f.write(reinterpret_cast<const char*>(&l), 2);
                        f.write(reinterpret_cast<const char*>(&r), 2);
                    }
                    totalSamples += n;
                    remain -= n;
                }

                if (chDef.contains("key_off"))
                    applyRegs(api, eng, chip_id, chDef["key_off"], chPort);

                // rest 期間レンダリング
                remain = (uint32_t)((uint64_t)rest_ms * sampleRate / 1000);
                while (remain > 0) {
                    const uint32_t n = (remain < BLOCK) ? remain : BLOCK;
                    api.Generate(eng, bufL.data(), bufR.data(), n);
                    for (uint32_t s = 0; s < n; ++s) {
                        const int16_t l = floatToS16(bufL[s]);
                        const int16_t r = floatToS16(bufR[s]);
                        f.write(reinterpret_cast<const char*>(&l), 2);
                        f.write(reinterpret_cast<const char*>(&r), 2);
                    }
                    totalSamples += n;
                    remain -= n;
                }
            }
        }
        printf("\n");
    }

    // WAV ヘッダーを正しい dataSize で上書き
    const uint32_t dataSize = totalSamples * 2 * 2; // stereo * 2bytes/sample
    f.seekp(0);
    writeWavHeader(f, sampleRate, 2, dataSize);
    f.close();

    printf("WAV exported: %s (%u samples, %.2f sec)\n",
           outPath.c_str(), totalSamples,
           (double)totalSamples / sampleRate);
    return true;
}

// =========================================================
//  テストスイートの1エントリを実行する
// 戻り値: 成功=true
static bool runSuiteEntry(
    const SuiteEntry& entry,
    const char* defaultDll)
{
    const char* enginePath = entry.engine.empty() ? defaultDll : entry.engine.c_str();
    const uint32_t sampleRate = entry.sampleRate;
    const char* deviceName = entry.device.empty() ? nullptr : entry.device.c_str();

    printf("----------------------------------------\n");
    printf("Engine      : %s\n", enginePath);
    printf("Sample rate : %u Hz\n", sampleRate);
    printf("Device      : %s\n", deviceName ? deviceName : "(default)");
    if (!entry.outputWav.empty())
        printf("Output WAV  : %s\n", entry.outputWav.c_str());
    printf("Patches     :\n");
    for (const auto& p : entry.patches)
        printf("  %s\n", p.c_str());
    printf("----------------------------------------\n");
    fflush(stdout);

    // DLL ロード
    FmEngineApi api;
    DllHandle   dllHandle = nullptr;
    printf("Loading engine: %s\n", enginePath);
    if (!loadApi(enginePath, api, dllHandle)) return false;
    printf("Engine loaded.\n\n");

    // エンジン作成
    FmEngineHandle eng = api.Create(sampleRate);
    if (!eng) {
        fputs("FmEngine_Create failed\n", stderr);
        dllClose(dllHandle);
        return false;
    }
    printf("Sample rate: %u Hz\n\n", sampleRate);

    // 対応チップ一覧
    {
        const uint32_t n = api.Inquiry(eng);
        printf("Supported chips (%u):", n);
        for (uint32_t i = 0; i < n; ++i)
            printf(" %s", api.GetSupportedChip(eng, i));
        printf("\n\n");
    }

    // パッチファイル読み込み・チップ追加 (ストリーム開始前 / WAV モードでも同じ)
    std::vector<FileContext> ctxs(entry.patches.size());
    for (size_t i = 0; i < entry.patches.size(); ++i) {
        ctxs[i].path  = entry.patches[i];
        ctxs[i].valid = loadJson(entry.patches[i].c_str(), ctxs[i].root);
        if (ctxs[i].valid && ctxs[i].root.contains("chips") &&
            ctxs[i].root["chips"].is_object()) {
            std::vector<std::string> visited{ ctxs[i].path };
            resolveChipRefs(ctxs[i].root["chips"], ctxs[i].path, visited);
        }
        addChipsFromFile(api, ctxs[i], eng);
    }

    // --- WAV エクスポートモード ---
    if (!entry.outputWav.empty()) {
        printf("Rendering offline...\n");
        fflush(stdout);
        const bool ok = renderToWav(api, eng, ctxs, sampleRate, entry.outputWav);
        api.Destroy(eng);
        dllClose(dllHandle);
        return ok;
    }

    // --- リアルタイム再生モード ---
    RtAudio audio;
    if (audio.getDeviceCount() == 0) {
        fputs("RtAudio: no audio devices found\n", stderr);
        api.Destroy(eng);
        dllClose(dllHandle);
        return false;
    }
    printf("Audio devices:\n");
    unsigned int selectedId = audio.getDefaultOutputDevice();
    {
        const unsigned int defaultId = audio.getDefaultOutputDevice();
        for (unsigned int id : audio.getDeviceIds()) {
            RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
            if (info.outputChannels < 2) continue;
            const bool isDef = (id == defaultId);
            printf("  [%u] %s%s\n", id, info.name.c_str(), isDef ? " (default)" : "");
            if (deviceName && info.name.find(deviceName) != std::string::npos)
                selectedId = id;
        }
    }
    if (selectedId == 0) {
        for (unsigned int id : audio.getDeviceIds()) {
            if (audio.getDeviceInfo(id).outputChannels >= 2) { selectedId = id; break; }
        }
    }
    printf("\nUsing device id=%u\n\n", selectedId);

    AudioState audioState;
    audioState.eng = eng;
    audioState.api = &api;

    RtAudio::StreamParameters outParams;
    outParams.deviceId     = selectedId;
    outParams.nChannels    = 2;
    outParams.firstChannel = 0;

    unsigned int bufferFrames = 512;
    printf("Opening stream (device=%u, rate=%u, frames=%u)...\n",
           selectedId, sampleRate, bufferFrames);
    fflush(stdout);

    RtAudioErrorType err = audio.openStream(
        &outParams, nullptr,
        RTAUDIO_FLOAT32, sampleRate,
        &bufferFrames,
        rtAudioCallback, &audioState);
    if (err != RTAUDIO_NO_ERROR) {
        fprintf(stderr, "RtAudio::openStream failed: %s\n", audio.getErrorText().c_str());
        api.Destroy(eng);
        dllClose(dllHandle);
        return false;
    }

    err = audio.startStream();
    if (err != RTAUDIO_NO_ERROR) {
        fprintf(stderr, "RtAudio::startStream failed: %s\n", audio.getErrorText().c_str());
        audio.closeStream();
        api.Destroy(eng);
        dllClose(dllHandle);
        return false;
    }
    printf("Stream started (bufferFrames=%u)\n\n", bufferFrames);
    fflush(stdout);

    // 発音テスト
    for (const auto& ctx : ctxs)
        playChipsFromFile(api, ctx, eng);

    // 停止・解放
    if (audio.isStreamRunning()) audio.stopStream();
    if (audio.isStreamOpen())    audio.closeStream();
    api.Destroy(eng);
    dllClose(dllHandle);
    return true;
}

// =========================================================
//  main
// =========================================================
int main(int argc, char* argv[]) {
    std::vector<const char*> jsonFiles;
    uint32_t    sampleRate = 48000;
    const char* deviceName = nullptr;
    const char* enginePath = nullptr;  // -e で指定

    for (int i = 1; i < argc; ++i) {
        if      (strcmp(argv[i], "-r") == 0 && i+1 < argc) sampleRate = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) deviceName = argv[++i];
        else if (strcmp(argv[i], "-e") == 0 && i+1 < argc) enginePath = argv[++i];
        else if (argv[i][0] != '-')                        jsonFiles.push_back(argv[i]);
    }
    // デフォルト DLL 名
#ifdef _WIN32
    const char* defaultDll = "FmEngineApi.dll";
#elif defined(__APPLE__)
    const char* defaultDll = "libFmEngineApi.dylib";
#else
    const char* defaultDll = "libFmEngineApi.so";
#endif

    printf("FMEngineTest\n\n");

    // ① 各 JSON ファイルを先読みし、テストスイートか通常パッチかに振り分ける
    //    テストスイートが1つでも含まれていれば、全ファイルをスイートとして処理する。
    //    (通常パッチは後段で SuiteEntry に変換)

    if (jsonFiles.empty())
        jsonFiles.push_back("patches/all.json");

    // テストスイートエントリを収集
    std::vector<SuiteEntry> suiteEntries;
    // 通常パッチファイル (test_suite キーを持たないもの)
    std::vector<const char*> normalPatches;

    bool hasSuite = false;
    for (const char* f : jsonFiles) {
        json root;
        if (!loadJson(f, root)) continue;
        if (isTestSuiteJson(root)) {
            hasSuite = true;
            auto entries = parseTestSuite(root, enginePath, sampleRate, deviceName);
            suiteEntries.insert(suiteEntries.end(), entries.begin(), entries.end());
        } else {
            normalPatches.push_back(f);
        }
    }

    // 通常パッチが混在している場合は1エントリにまとめる
    if (!normalPatches.empty()) {
        SuiteEntry e;
        e.engine     = enginePath ? enginePath : "";
        e.sampleRate = sampleRate;
        e.device     = deviceName ? deviceName : "";
        for (const char* p : normalPatches)
            e.patches.push_back(p);
        suiteEntries.insert(suiteEntries.begin(), std::move(e));
    }

    if (suiteEntries.empty()) {
        fputs("No valid test entries found.\n", stderr);
        return 1;
    }

    // ② エントリ数を表示
    if (hasSuite) {
        printf("Test suite mode: %zu entries\n\n",  suiteEntries.size());
    }

    // ③ 各エントリを順に実行
    int failed = 0;
    for (size_t i = 0; i < suiteEntries.size(); ++i) {
        if (hasSuite)
            printf("\n=== Entry [%zu/%zu] ===\n", i + 1, suiteEntries.size());
        if (!runSuiteEntry(suiteEntries[i], defaultDll))
            ++failed;
    }

    printf("\nDone. (%zu entries, %d failed)\n",
           suiteEntries.size(), failed);
    return failed ? 1 : 0;
}
