// Iris Core —— 拼音封装（基于 cpp-pinyin）
// 把中文名转为无音调全拼/首字母，供 App/Bookmark Provider 预计算、Matcher 拼音匹配。
// dict 经 qrc 嵌入 exe，Init 时释放到便携数据目录（GetDataDir()/dict/）。
#pragma once

#include <string>
#include <string_view>

namespace iris::PinyinUtil {

/// 初始化：从 qrc 释放内置 dict 到 dataDir/dict，设置 cpp-pinyin 字典路径并创建 g2p。
/// 须在 ToFull/ToInitials 前调用一次（main.cpp 启动）。失败返回 false。
bool Init(const std::wstring& dataDir);

/// 中文名 → 无音调全拼连接（如 L"微信" → L"weixin"）；无汉字时原样返回。
std::wstring ToFull(std::wstring_view text);

/// 中文名 → 首字母连接（如 L"微信" → L"wx"）；无汉字时原样返回。
std::wstring ToInitials(std::wstring_view text);

} // namespace iris::PinyinUtil
