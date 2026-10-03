cmake_minimum_required(VERSION 3.20)
project(FMEngineTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
#  依存ライブラリ
#  vcpkg.json に列挙してあり、vcpkg のツールチェーンが configure 時に用意する。
#
#  FmEngineApi DLL は実行時に動的ロードするため、
#  ビルド時のリンクは不要。
# -------------------------------------------------------
find_package(nlohmann_json CONFIG)
find_package(RtAudio CONFIG)
if (NOT nlohmann_json_FOUND OR NOT RtAudio_FOUND)
    message(FATAL_ERROR
        "nlohmann_json or RtAudio not found\n"
        "  Configure with the vcpkg toolchain:\n"
        "       -DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake\n"
        "  A build directory reads the toolchain only when it is first configured.\n"
        "  For an existing one, add --fresh.")
endif()

# -------------------------------------------------------
#  include/FmEngineApi.h と docs/FmEngineApi.md の突き合わせ
#  どちらかを編集すると、次のビルドでもう一度走る。
# -------------------------------------------------------
include(cmake/CheckApiSymbols.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/FmEngineApi.h"
    "${CMAKE_SOURCE_DIR}/docs/FmEngineApi.md")

# =======================================================
#  ターゲット: FMEngineTest
#  FmEngineApi には静的リンクしない (実行時に LoadLibrary/dlopen)
#  Linux/macOS では dl ライブラリが必要
# =======================================================
add_executable(FMEngineTest src/main.cpp)

target_include_directories(FMEngineTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(FMEngineTest PRIVATE
    RtAudio::rtaudio
    nlohmann_json::nlohmann_json
)

if (UNIX)
    target_link_libraries(FMEngineTest PRIVATE dl)
endif()

if (MSVC)
    set_target_properties(FMEngineTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    target_compile_options(FMEngineTest PRIVATE /W4 /wd4100 /utf-8)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FMEngineTest PRIVATE -Wall -Wextra)
endif()

# -------------------------------------------------------
#  patches/ を実行ファイルと同じ場所にコピー
# -------------------------------------------------------
set(PATCHES_SRC_DIR "${CMAKE_SOURCE_DIR}/src/patches")
if (EXISTS "${PATCHES_SRC_DIR}")
    file(GLOB PATCH_FILES "${PATCHES_SRC_DIR}/*.json")
    foreach(PATCH_FILE ${PATCH_FILES})
        add_custom_command(TARGET FMEngineTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:FMEngineTest>/patches"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PATCH_FILE}"
                "$<TARGET_FILE_DIR:FMEngineTest>/patches/"
        )
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/patches")
    foreach(PATCH_FILE ${PATCH_FILES})
        configure_file("${PATCH_FILE}" "${CMAKE_BINARY_DIR}/bin/patches/" COPYONLY)
    endforeach()
endif()
