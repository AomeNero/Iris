cmake_minimum_required(VERSION 3.20)
project(Iris VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- 全局编译定义（Windows / MSVC）----
if(MSVC)
    # /Zc:preprocessor 会破坏 Qt 5.14.2 旧宏，故不启用；保留 /permissive-。
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(
        NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
    )
    # 静态链接 CRT (/MT) —— 详见 detailed-design §10.2 [已确认 J1]
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- Qt 配置（5.14.2 静态）----
set(CMAKE_PREFIX_PATH "D:/Qt/5.14.2_static")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Svg WinExtras)

# ---- nlohmann/json（header-only）----
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann)

# ---- 中间库输出目录：静态库等归 build/lib（随配置子目录 build/lib/Release），
#      保持 build/Release 仅含可发布成品（Iris.exe）。与 googletest 既有布局一致。----
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- SQLite（源码静态编译）----
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
target_compile_options(sqlite3 PRIVATE /W0)

# ---- cpp-pinyin（拼音搜索；静态链接，继承 Iris 全局 /MT CRT）----
set(BUILD_SHARED_LIBS OFF CACHE BOOL "cpp-pinyin static" FORCE)
set(CPP_PINYIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPP_PINYIN_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpp-pinyin)

# ---- Iris 可执行文件（全部 src/ 直接编译进 exe；GUI 子系统）----
file(GLOB_RECURSE IRIS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(Iris WIN32
    ${IRIS_SOURCES}
    resources/iris.rc
    resources/resources.qrc)
target_include_directories(Iris PRIVATE src)
target_compile_options(Iris PRIVATE /W4
                       /FI"core/QtStdextShim.h")  # Qt5.14.2 ↔ MSVC14.5x 兼容
target_link_libraries(Iris PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg Qt5::WinExtras
    Qt5::WinMain                              # Qt5 静态 WIN32 子系统的 WinMain
    nlohmann_json sqlite3 cpp-pinyin
    advapi32 shell32 ole32 oleaut32 user32 gdi32 imm32 uuid)
# 静态 Qt5：排除 QTiffPlugin（其依赖 Qt5::Zlib 在本静态 Qt 安装中缺失）
qt5_import_plugins(Iris EXCLUDE Qt5::QTiffPlugin)
set_target_properties(Iris PROPERTIES
    WIN32_EXECUTABLE TRUE          # GUI 子系统，不显示控制台
    VS_DPI_AWARE TRUE)
# 清单由 resources/iris.rc 提供，关闭链接器自动生成的 manifest 避免 CVT1100 重复资源。
target_link_options(Iris PRIVATE /MANIFEST:NO)
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")

# ---- 测试 ----
enable_testing()
add_subdirectory(test)
