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
// JSON フォーマット:
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

#include "RtAudio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#  include <time.h>
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
    if (jsonFiles.empty())
        jsonFiles.push_back("patches/all.json");

    // デフォルト DLL 名
#ifdef _WIN32
    const char* defaultDll = "FmEngineApi.dll";
#elif defined(__APPLE__)
    const char* defaultDll = "libFmEngineApi.dylib";
#else
    const char* defaultDll = "libFmEngineApi.so";
#endif
    if (!enginePath) enginePath = defaultDll;

    // ① DLL を動的ロード
    FmEngineApi api;
    DllHandle   dllHandle = nullptr;
    printf("FMEngineTest\n");
    printf("Loading engine: %s\n", enginePath);
    if (!loadApi(enginePath, api, dllHandle)) return 1;
    printf("Engine loaded.\n\n");

    // ② エンジン作成
    FmEngineHandle eng = api.Create(sampleRate);
    if (!eng) { fputs("FmEngine_Create failed\n", stderr); return 1; }
    printf("Sample rate: %u Hz\n\n", sampleRate);

    // 対応チップ一覧を表示
    {
        const uint32_t n = api.Inquiry(eng);
        printf("Supported chips (%u):", n);
        for (uint32_t i = 0; i < n; ++i)
            printf(" %s", api.GetSupportedChip(eng, i));
        printf("\n\n");
    }

    // ③ RtAudio デバイス列挙
    RtAudio audio;
    if (audio.getDeviceCount() == 0) {
        fputs("RtAudio: no audio devices found\n", stderr);
        api.Destroy(eng);
        dllClose(dllHandle);
        return 1;
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

    // ④ 全ファイルを読み込み、全チップを先に追加 (ストリーム開始前)
    std::vector<FileContext> ctxs(jsonFiles.size());
    for (size_t i = 0; i < jsonFiles.size(); ++i) {
        ctxs[i].path  = jsonFiles[i];
        ctxs[i].valid = loadJson(jsonFiles[i], ctxs[i].root);
        if (ctxs[i].valid && ctxs[i].root.contains("chips") &&
            ctxs[i].root["chips"].is_object()) {
            std::vector<std::string> visited{ ctxs[i].path };
            resolveChipRefs(ctxs[i].root["chips"], ctxs[i].path, visited);
        }
        addChipsFromFile(api, ctxs[i], eng);
    }

    // ⑤ RtAudio ストリーム開始 (全 AddChip 完了後)
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
        return 1;
    }

    err = audio.startStream();
    if (err != RTAUDIO_NO_ERROR) {
        fprintf(stderr, "RtAudio::startStream failed: %s\n", audio.getErrorText().c_str());
        audio.closeStream();
        api.Destroy(eng);
        dllClose(dllHandle);
        return 1;
    }
    printf("Stream started (bufferFrames=%u)\n\n", bufferFrames);
    fflush(stdout);

    // ⑥ 各ファイルを発音テスト
    for (const auto& ctx : ctxs)
        playChipsFromFile(api, ctx, eng);

    // ⑦ 停止・解放
    if (audio.isStreamRunning()) audio.stopStream();
    if (audio.isStreamOpen())    audio.closeStream();
    api.Destroy(eng);
    dllClose(dllHandle);
    printf("Done.\n");
    return 0;
}
