// Iris Core —— 字符串工具实现
#include "core/StringUtil.h"

#include <windows.h>

#include <cwctype>
#include <string>

namespace iris::StringUtil {

std::wstring ToLower(std::wstring_view sv) {
    std::wstring r(sv);
    // 仅 ASCII 区转小写；搜索匹配本身用 _wcsnicmp，这里用于建立小写关键词。
    for (auto& c : r)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return r;
}

std::vector<std::wstring> SplitTokens(std::wstring_view sv) {
    std::vector<std::wstring> tokens;
    std::size_t i = 0, n = sv.size();
    while (i < n) {
        while (i < n && std::iswspace(static_cast<wint_t>(sv[i]))) ++i;
        std::size_t start = i;
        while (i < n && !std::iswspace(static_cast<wint_t>(sv[i]))) ++i;
        if (i > start) tokens.emplace_back(sv.substr(start, i - start));
    }
    return tokens;
}

bool IsCJK(wchar_t c) {
    // CJK 统一表意文字基本区
    return c >= 0x4E00 && c <= 0x9FFF;
}

std::wstring GetPinyinInitials(std::wstring_view /*sv*/) {
    return {};  // P3 stub，后续查表实现
}

uint32_t GetPathDepth(std::wstring_view path) {
    uint32_t depth = 0;
    const std::size_t n = path.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            if (i + 1 < n && path[i + 1] != L'\\' && path[i + 1] != L'/') ++depth;
        }
    }
    return depth;
}

std::wstring ExtractFileName(std::wstring_view fullPath) {
    if (fullPath.empty()) return {};
    const std::size_t pos = fullPath.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) return std::wstring(fullPath);
    return std::wstring(fullPath.substr(pos + 1));
}

std::wstring ExtractExtension(std::wstring_view fileName) {
    const std::wstring base = ExtractFileName(fileName);
    const std::size_t pos = base.find_last_of(L'.');
    if (pos == std::wstring::npos || pos == 0) return {};
    std::wstring ext = base.substr(pos);
    for (auto& c : ext)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return ext;
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), len);
    return wide;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                        static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), len, nullptr, nullptr);
    return utf8;
}

} // namespace iris::StringUtil
