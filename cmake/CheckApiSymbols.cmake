# include/FmEngineApi.h が宣言する関数と、docs/FmEngineApi.md の
# 「エクスポートシンボル一覧」の節に並ぶ名前が、同じ集合であることを確かめる。
# 比べるのは名前だけで、引数の型は比べない。
#
# CMakeLists.txt から include するほか、単独でも走らせられる:
#   cmake -P cmake/CheckApiSymbols.cmake

function(fmengine_check_api_symbols)
    set(root    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(heading "## エクスポートシンボル一覧")

    file(READ "${root}/include/FmEngineApi.h" text)
    # コメントの中に出てくる関数名は宣言ではない
    string(REGEX REPLACE "//[^\n]*" "" text "${text}")
    string(REGEX MATCHALL "FmEngine_[A-Za-z0-9]+" header_syms "${text}")

    file(READ "${root}/docs/FmEngineApi.md" text)
    string(FIND "${text}" "${heading}" begin)
    if (begin EQUAL -1)
        message(FATAL_ERROR
            "docs/FmEngineApi.md: the export symbol list heading was not found")
    endif()
    string(LENGTH "${heading}" skip)
    math(EXPR begin "${begin} + ${skip}")
    string(SUBSTRING "${text}" ${begin} -1 text)
    string(FIND "${text}" "\n## " end)
    if (NOT end EQUAL -1)
        string(SUBSTRING "${text}" 0 ${end} text)
    endif()
    string(REGEX MATCHALL "FmEngine_[A-Za-z0-9]+" doc_syms "${text}")

    # どちらかが空なら、下の差分は取り出しの失敗を「一致」と見せてしまう
    if (NOT header_syms OR NOT doc_syms)
        message(FATAL_ERROR
            "CheckApiSymbols: no FmEngine_* names found in the header or in the spec")
    endif()

    list(REMOVE_DUPLICATES header_syms)
    list(REMOVE_DUPLICATES doc_syms)
    set(only_header ${header_syms})
    list(REMOVE_ITEM only_header ${doc_syms})
    set(only_doc ${doc_syms})
    list(REMOVE_ITEM only_doc ${header_syms})

    if (only_header OR only_doc)
        string(REPLACE ";" " " only_header "${only_header}")
        string(REPLACE ";" " " only_doc "${only_doc}")
        message(FATAL_ERROR
            "include/FmEngineApi.h and the export symbol list in docs/FmEngineApi.md differ\n"
            "  only in the header: ${only_header}\n"
            "  only in the spec  : ${only_doc}")
    endif()

    list(LENGTH header_syms count)
    message(STATUS "FmEngineApi: header and spec list the same ${count} symbols")
endfunction()

fmengine_check_api_symbols()
