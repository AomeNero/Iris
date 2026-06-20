// Iris Core —— 字符串工具
// 设计依据: doc/detailed-design.md §5.3
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace iris::StringUtil {

/// 转小写（搜索优化：仅 ASCII 区直接转，其余不动）
std::wstring ToLower(std::wstring_view sv);

/// 按空白字符分词（多关键词以空格分隔）
std::vector<std::wstring> SplitTokens(std::wstring_view sv);

/// 是否为中文字符（CJK 统一表意文字基本区）
bool IsCJK(wchar_t c);

/// 中文拼音首字母（P3 stub：暂返回空串，匹配时不贡献分数）
std::wstring GetPinyinInitials(std::wstring_view sv);

/// 路径深度（\ 或 / 分隔符计数，根为 0）
uint32_t GetPathDepth(std::wstring_view path);

/// 提取纯文件名（不含目录），自动处理 \ 与 /
std::wstring ExtractFileName(std::wstring_view fullPath);

/// 提取扩展名（含点，转小写），无扩展返回空
std::wstring ExtractExtension(std::wstring_view fileName);

/// 宽窄转换
std::wstring Utf8ToWide(std::string_view utf8);
std::string  WideToUtf8(std::wstring_view wide);

} // namespace iris::StringUtil
