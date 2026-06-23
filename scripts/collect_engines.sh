#!/usr/bin/env bash
# collect_engines.sh
# FmEngineApi 互換エンジン SO/dylib を収集してビルド先ディレクトリにコピーする
#
# 使い方:
#   bash scripts/collect_engines.sh [出力ディレクトリ]
#   省略時は build/bin を使用
#
# 前提:
#   - git, cmake, make (または ninja) が PATH に通っていること
#   - Linux: g++ / clang++
#   - macOS: Xcode Command Line Tools

set -euo pipefail

# --- 出力ディレクトリ ---
OUT_DIR="${1:-build/bin}"

# --- ビルド対象エンジンリスト ---
ENGINES=(
    YMEngine
    NukedEngine
    FMgenEngine
    DSAemuEngine
    DBOPLEngine
    SAASoundEngine
)

ENGINES_DIR="engines"
GITHUB_BASE="https://github.com/madscient"

# OS 判定
case "$(uname -s)" in
    Darwin*) OS=mac ;;
    *)       OS=linux ;;
esac

echo "============================================================"
echo " FMEngineTest: collect_engines.sh"
echo " Output dir : ${OUT_DIR}"
echo " Engines dir: ${ENGINES_DIR}"
echo " OS         : ${OS}"
echo "============================================================"
echo

mkdir -p "${OUT_DIR}" "${ENGINES_DIR}"

SUCCESS=()
FAILED=()

for ENGINE in "${ENGINES[@]}"; do
    echo "------------------------------------------------------------"
    echo " Engine: ${ENGINE}"
    echo "------------------------------------------------------------"

    REPO_DIR="${ENGINES_DIR}/${ENGINE}"
    BUILD_DIR="${REPO_DIR}/build"

    # clone または pull
    if [[ -d "${REPO_DIR}/.git" ]]; then
        echo "  Updating ${REPO_DIR} ..."
        git -C "${REPO_DIR}" pull --ff-only || { echo "  [WARN] git pull failed, continuing with existing"; }
    else
        echo "  Cloning ${GITHUB_BASE}/${ENGINE} ..."
        if ! git clone --depth 1 "${GITHUB_BASE}/${ENGINE}" "${REPO_DIR}"; then
            echo "  [ERROR] git clone failed for ${ENGINE}"
            FAILED+=("${ENGINE}")
            echo
            continue
        fi
    fi

    # submodule
    git -C "${REPO_DIR}" submodule update --init --recursive

    # cmake configure
    CMAKE_ARGS=(-B "${BUILD_DIR}" -S "${REPO_DIR}" -DCMAKE_BUILD_TYPE=Release)
    if [[ "${OS}" == "mac" ]]; then
        CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="$(uname -m)")
    fi
    if ! cmake "${CMAKE_ARGS[@]}"; then
        echo "  [ERROR] cmake configure failed for ${ENGINE}"
        FAILED+=("${ENGINE}")
        echo
        continue
    fi

    # cmake build (並列)
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
    if ! cmake --build "${BUILD_DIR}" --parallel "${NPROC}"; then
        echo "  [ERROR] cmake build failed for ${ENGINE}"
        FAILED+=("${ENGINE}")
        echo
        continue
    fi

    # SO/dylib コピー
    COPIED=0
    if [[ "${OS}" == "mac" ]]; then
        EXT="dylib"
    else
        EXT="so"
    fi

    while IFS= read -r -d '' LIB; do
        echo "  Copying ${LIB} -> ${OUT_DIR}/"
        cp -f "${LIB}" "${OUT_DIR}/"
        COPIED=1
    done < <(find "${BUILD_DIR}" -name "*.${EXT}" -print0 2>/dev/null)

    if [[ "${COPIED}" -eq 0 ]]; then
        echo "  [WARN] No .${EXT} found in build output for ${ENGINE}"
    else
        SUCCESS+=("${ENGINE}")
    fi
    echo
done

echo "============================================================"
echo " Results"
echo "============================================================"
if [[ ${#SUCCESS[@]} -gt 0 ]]; then
    echo " Success: ${SUCCESS[*]}"
fi
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo " Failed : ${FAILED[*]}"
    exit 1
fi
echo "============================================================"
