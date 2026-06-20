// Iris Engine —— QueryParser 实现
#include "engine/QueryParser.h"

#include "core/StringUtil.h"

#include <cwctype>

namespace iris {

ParsedQuery QueryParser::Parse(std::wstring_view rawText) {
    ParsedQuery q;

    // 去除前导空白后判断前缀
    std::size_t i = 0;
    while (i < rawText.size() && std::iswspace(static_cast<wint_t>(rawText[i]))) ++i;

    if (i < rawText.size()) {
        const wchar_t ch = rawText[i];
        if (ch == L'#') {
            q.filterType = ItemType::APPLICATION;
            ++i;  // 消费前缀
        } else if (ch == L'@') {
            q.filterType = ItemType::BOOKMARK;
            ++i;
        }
    }

    // 剩余部分分词并转小写
    std::wstring_view rest = rawText.substr(i);
    for (auto& tok : StringUtil::SplitTokens(rest)) {
        q.keywords.push_back(StringUtil::ToLower(tok));
    }
    return q;
}

} // namespace iris
