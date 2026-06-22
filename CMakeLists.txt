cmake_minimum_required(VERSION 3.20)
project(FMEngineTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
#  依存ライブラリ
#  git submodule add https://github.com/nlohmann/json  extern/nlohmann_json
#  git submodule add https://github.com/thestk/rtaudio extern/rtaudio
#
#  FmEngineApi DLL は実行時に動的ロードするため、
#  ビルド時のリンクは不要。
# -------------------------------------------------------
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/extern/nlohmann_json/single_include" CACHE PATH "nlohmann/json include dir")
set(RTAUDIO_DIR  "${CMAKE_SOURCE_DIR}/extern/rtaudio" CACHE PATH "RtAudio dir")

# -------------------------------------------------------
#  RtAudio
#  注意: RtAudio は CMAKE_DEBUG_POSTFIX を "d" に設定するため
#        add_subdirectory の前後でリセットする。
# -------------------------------------------------------
if (EXISTS "${RTAUDIO_DIR}/CMakeLists.txt")
    set(RTAUDIO_BUILD_TESTING        OFF CACHE BOOL   "" FORCE)
    set(RTAUDIO_TARGETNAME_UNINSTALL "RtAudioUninstall" CACHE STRING "" FORCE)
    set(_saved_debug_postfix "${CMAKE_DEBUG_POSTFIX}")
    add_subdirectory(${RTAUDIO_DIR} ${CMAKE_BINARY_DIR}/rtaudio EXCLUDE_FROM_ALL)
    set(CMAKE_DEBUG_POSTFIX "${_saved_debug_postfix}" CACHE STRING "" FORCE)
    if (TARGET rtaudio AND MSVC)
        set_target_properties(rtaudio PROPERTIES
            DEBUG_POSTFIX            ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin"
        )
    endif()
    message(STATUS "RtAudio found: ${RTAUDIO_DIR}")
else()
    message(FATAL_ERROR
        "RtAudio not found at ${RTAUDIO_DIR}\n"
        "  Run: git submodule add https://github.com/thestk/rtaudio extern/rtaudio\n"
        "       git submodule update --init --recursive")
endif()

# =======================================================
#  ターゲット: FMEngineTest
#  FmEngineApi には静的リンクしない (実行時に LoadLibrary/dlopen)
#  Linux/macOS では dl ライブラリが必要
# =======================================================
add_executable(FMEngineTest src/main.cpp)

target_include_directories(FMEngineTest PRIVATE
    ${NLOHMANN_DIR}
    ${RTAUDIO_DIR}
)

target_link_libraries(FMEngineTest PRIVATE rtaudio)

if (UNIX)
    target_link_libraries(FMEngineTest PRIVATE dl)
endif()

if (MSVC)
    set_target_properties(FMEngineTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        DEBUG_POSTFIX            ""
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
