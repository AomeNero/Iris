// Iris Engine —— Matcher 实现
// 评分规则 [已确认 E3]：title+subtitle+path 全匹配
#include "engine/Matcher.h"

#include <algorithm>
#include <cwchar>

namespace iris {

namespace {

// case-insensitive 前缀判断：text 是否以 kw 开头
bool StartsWithCI(std::wstring_view text, std::wstring_view kw) {
    if (kw.size() > text.size()) return false;
    return _wcsnicmp(text.data(), kw.data(), kw.size()) == 0;
}

// case-insensitive 子串判断
bool ContainsCI(std::wstring_view text, std::wstring_view kw) {
    if (kw.empty()) return true;
    if (kw.size() > text.size()) return false;
    for (std::size_t i = 0; i + kw.size() <= text.size(); ++i) {
        if (_wcsnicmp(text.data() + i, kw.data(), kw.size()) == 0) return true;
    }
    return false;
}

// path 段开头匹配：kw 出现在某段（\ 或 / 之后）开头
bool PathSegmentStartsCI(std::wstring_view path, std::wstring_view kw) {
    if (StartsWithCI(path, kw)) return true;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            if (StartsWithCI(path.substr(i + 1), kw)) return true;
        }
    }
    return false;
}

// keyword 是否全 ASCII（拼音匹配只对 ASCII keyword 有意义）
bool IsAscii(std::wstring_view s) {
    for (wchar_t c : s)
        if (static_cast<unsigned>(c) > 127) return false;
    return true;
}

// 拼音匹配分数：全拼前缀 9 / 首字母前缀 7 / 全拼包含 5；不匹配 0。
// full/initials 为空（如 FileProvider）则不命中。
int PinyinScore(std::wstring_view full, std::wstring_view initials, std::wstring_view kw) {
    if (!full.empty() && StartsWithCI(full, kw)) return 9;
    if (!initials.empty() && StartsWithCI(initials, kw)) return 7;
    if (!full.empty() && ContainsCI(full, kw)) return 5;
    return 0;
}

} // namespace

int Matcher::MatchSingle(std::wstring_view title, std::wstring_view subtitle,
                         std::wstring_view path,
                         std::wstring_view pinyinFull, std::wstring_view pinyinInitials,
                         const std::vector<std::wstring>& keywords) {
    if (keywords.empty()) return 1;  // 仅类型过滤：全部纳入，由 Ranker 排序

    int positionScore = 0;
    int matched = 0;
    for (const auto& kw : keywords) {
        if (StartsWithCI(title, kw)) {
            positionScore += 30; ++matched;
        } else if (PathSegmentStartsCI(path, kw)) {
            positionScore += 20; ++matched;
        } else if (ContainsCI(title, kw)) {
            positionScore += 10; ++matched;
        } else if (ContainsCI(subtitle, kw)) {
            positionScore += 7; ++matched;
        } else if (ContainsCI(path, kw)) {
            positionScore += 5; ++matched;
        } else if (IsAscii(kw)) {
            // 拼音匹配（App/Bookmark 预计算了拼音；File 返回空 → PinyinScore=0 不命中）
            const int ps = PinyinScore(pinyinFull, pinyinInitials, kw);
            if (ps > 0) { positionScore += ps; ++matched; }
        }
    }

    // 任一关键词未命中 → 该条目 0 分
    if (matched < static_cast<int>(keywords.size())) return 0;

    const int coverageScore = static_cast<int>(
        (static_cast<double>(matched) / keywords.size()) * 15);
    const int raw = positionScore + coverageScore;
    return raw > 100 ? 100 : raw;
}

std::vector<MatchResult> Matcher::Match(ISearchableProvider& provider,
                                        const ParsedQuery& query,
                                        const std::atomic<bool>& cancelled) {
    std::vector<MatchResult> results;

    const std::size_t startIdx = query.keywords.empty()
        ? 0
        : provider.FindFirstPrefix(query.keywords[0]);
    const std::size_t count = provider.GetCount();

    // 前缀扫描：当首关键词为前缀匹配时，连续命中区间结束即可停（带关键词时优化）
    const bool prefixScan = !query.keywords.empty();
    bool lastWasPrefixHit = false;

    for (std::size_t idx = startIdx; idx < count; ++idx) {
        if ((idx & 0x3FF) == 0 && cancelled.load(std::memory_order_relaxed))
            return results;

        const ResultItem item = provider.BuildResultItem(idx);
        const int score = MatchSingle(item.title, item.subtitle, item.path,
                                      provider.GetPinyinFull(idx),
                                      provider.GetPinyinInitials(idx),
                                      query.keywords);
        if (score > 0) {
            MatchResult mr;
            mr.provider = &provider;
            mr.entryIndex = idx;
            mr.rawScore = score;
            results.push_back(mr);
            // 仅 title 前缀命中才延续前缀扫描区；拼音/包含命中不算——中文拼音命中散布在
            // 非前缀区，若记为前缀命中，离开前缀区时会误 break，错过后续拼音命中条目。
            lastWasPrefixHit = StartsWithCI(item.title, query.keywords[0]);
        } else if (prefixScan) {
            // 前缀扫描区间：一旦越过前缀命中区且连续未命中，可提前结束
            if (lastWasPrefixHit) {
                const std::wstring_view name = item.title;  // 近似：title 为文件名
                if (!name.empty() && !StartsWithCI(name, query.keywords[0])) {
                    // 仅当确实离开前缀区才跳出（保守：要求连续未命中且非前缀）
                    break;
                }
            }
        }
    }
    return results;
}

} // namespace iris
