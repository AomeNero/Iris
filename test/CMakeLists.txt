# 测试构建：集成 GoogleTest
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest
                 ${CMAKE_BINARY_DIR}/googletest)

include(GoogleTest)

# 测试直接编译被测源文件（Iris 是 WIN32 GUI 程序，含 WinMain，不能与测试 main 共存）。
# 用法: add_iris_test(<name> [NOMAIN] <srcs...> [LINK <lib>...])
#   - NOMAIN: 不链接 gtest_main（测试自带 main，用于需要 QCoreApplication 的场景）
#   - 含 Q_OBJECT 的头文件须列入源（供 AUTOMOC 生成 moc）
function(add_iris_test name)
    set(args ${ARGN})
    set(test_srcs "")
    set(link_libs "")
    set(no_main FALSE)
    set(in_link FALSE)
    foreach(arg IN LISTS args)
        if(arg STREQUAL "NOMAIN")
            set(no_main TRUE)
        elseif(arg STREQUAL "LINK")
            set(in_link TRUE)
        elseif(in_link)
            list(APPEND link_libs "${arg}")
        else()
            list(APPEND test_srcs "${arg}")
        endif()
    endforeach()

    add_executable(${name} ${test_srcs})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${name} PRIVATE /W4
                          /FI"core/QtStdextShim.h")  # 测试含 Iris 头(含 Qt)需 shim
    if(no_main)
        target_link_libraries(${name} PRIVATE gtest gmock Qt5::Core nlohmann_json ${link_libs})
    else()
        target_link_libraries(${name} PRIVATE gtest gmock gtest_main Qt5::Core nlohmann_json ${link_libs})
    endif()
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

set(SRC ${CMAKE_SOURCE_DIR}/src)

# ---- 模块 2：data-model（纯头文件）----
add_iris_test(test_data_model test_data_model.cpp)

# ---- 模块 3：Config ----
add_iris_test(test_config
    test_config.cpp ${SRC}/core/Config.cpp ${SRC}/core/WinUtil.cpp
    LINK shell32 ole32 oleaut32 advapi32 user32)

# ---- 模块 4：FileProvider::ShouldExclude ----
add_iris_test(test_file_provider
    test_file_provider.cpp
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/provider/FileProvider.h
    ${SRC}/provider/FileProvider.cpp
    ${SRC}/core/StringUtil.cpp
    ${SRC}/core/Logger.cpp
    ${SRC}/core/WinUtil.cpp
    LINK shell32 ole32 oleaut32 advapi32 user32)

# ---- 模块 5：搜索引擎 ----
add_iris_test(test_query_parser
    test_query_parser.cpp
    ${SRC}/engine/QueryParser.cpp
    ${SRC}/core/StringUtil.cpp)

add_iris_test(test_matcher
    test_matcher.cpp
    mock_provider.h
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/engine/Matcher.cpp)

add_iris_test(test_ranker
    test_ranker.cpp
    mock_provider.h
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/engine/Matcher.cpp
    ${SRC}/engine/Ranker.cpp
    ${SRC}/core/HistoryStore.cpp
    ${SRC}/core/StringUtil.cpp
    ${SRC}/core/Logger.cpp
    LINK sqlite3)

add_iris_test(test_search_engine NOMAIN
    test_search_engine.cpp
    mock_provider.h
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/engine/SearchEngine.h
    ${SRC}/engine/SearchEngine.cpp
    ${SRC}/engine/QueryParser.cpp
    ${SRC}/engine/Matcher.cpp
    ${SRC}/engine/Ranker.cpp
    ${SRC}/core/ThreadPool.cpp
    ${SRC}/core/HistoryStore.cpp
    ${SRC}/core/StringUtil.cpp
    ${SRC}/core/Logger.cpp
    LINK sqlite3)

# ---- 模块 6b：BookmarkProvider（JSON 解析）----
add_iris_test(test_bookmark_provider
    test_bookmark_provider.cpp
    ${SRC}/provider/BookmarkProvider.h
    ${SRC}/provider/BookmarkProvider.cpp
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/core/StringUtil.cpp
    ${SRC}/core/Logger.cpp
    ${SRC}/core/WinUtil.cpp
    LINK shell32 ole32 oleaut32 advapi32 user32)

# ---- 模块 6c：AppProvider（去重逻辑；.lnk/COM 解析留端到端）----
add_iris_test(test_app_provider
    test_app_provider.cpp
    ${SRC}/provider/AppProvider.h
    ${SRC}/provider/AppProvider.cpp
    ${SRC}/provider/IProvider.h
    ${SRC}/provider/ISearchableProvider.h
    ${SRC}/core/StringUtil.cpp
    ${SRC}/core/Logger.cpp
    ${SRC}/core/WinUtil.cpp
    LINK shell32 ole32 oleaut32 advapi32 user32)
