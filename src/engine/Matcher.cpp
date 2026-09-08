// Iris Engine —— Matcher 实现
// 评分规则 [已确认 E3]：title+subtitle+path 全匹配
#include "engine/Matcher.h"

#include "provider/TrigramIndex.h"

#include <algorithm>
#include <cwchar>

// SSE4.2 加速字符串比较（_mm_cmpestri 批量 8 wchar_t）
#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif

namespace iris {

namespace {

// case-insensitive 前缀判断：text 是否以 kw 开头
bool StartsWithCI(std::wstring_view text, std::wstring_view kw) {
    if (kw.size() > text.size()) return false;
    return _wcsnicmp(text.data(), kw.data(), kw.size()) == 0;
}

// case-insensitive 子串判断（SIMD 加速 + 标量回退）
bool ContainsCI(std::wstring_view text, std::wstring_view kw) {
    if (kw.empty()) return true;
    if (kw.size() > text.size()) return false;

#ifdef __SSE4_2__
    // SSE4.2: 用 _mm_cmpestri 批量比较 8 个 wchar_t（128-bit）
    if (kw.size() <= 8 && text.size() >= 8) {
        // 构造 keyword 向量（小写）
        alignas(16) wchar_t kwLower[8] = {};
        for (size_t i = 0; i < kw.size(); ++i)
            kwLower[i] = static_cast<wchar_t>(towlower(static_cast<wint_t>(kw[i])));
        __m128i vk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(kwLower));

        const size_t limit = text.size() - 8;
        for (size_t i = 0; i <= limit; i += 8) {
            // 加载 8 个 wchar_t 并转小写
            __m128i vt = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text.data() + i));
            // 简化：先用 _mm_cmpestrw 做子串搜索（SSE4.2 string operations）
            // _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_EQUAL_ORDERED | _SIDD_WORD
            int idx = _mm_cmpestri(vk, static_cast<int>(kw.size()),
                                   vt, 8,
                                   _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_EQUAL_EACH | _SIDD_WORD);
            if (idx < 8) return true;
        }
    }
#endif

    // 标量回退
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
        // 前缀匹配（最高优先级）
        if (StartsWithCI(title, kw)) {
            positionScore += 30; ++matched;
        } else if (!path.empty() && PathSegmentStartsCI(path, kw)) {
            positionScore += 20; ++matched;
        } else if (ContainsCI(title, kw)) {
            positionScore += 10; ++matched;
        } else if (!subtitle.empty() && ContainsCI(subtitle, kw)) {
            positionScore += 7; ++matched;
        } else if (!path.empty() && ContainsCI(path, kw)) {
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
    if (query.keywords.empty()) return results;

    const auto* trigram = provider.GetTrigramIndex();

    // 有 TrigramIndex 且首关键词 >= 3 字符：走倒排索引（子串搜索 O(K)）
    if (trigram && trigram->EntryCount() > 0 && query.keywords[0].size() >= 3) {
        auto candidates = provider.SearchTrigram(query.keywords[0]);  // 含 delta 增量合并
        for (uint32_t idx : candidates) {
            if ((idx & 0x3FF) == 0 && cancelled.load(std::memory_order_relaxed))
                return results;

            const int score = MatchSingle(
                provider.GetSearchText(idx), {}, {},
                provider.GetPinyinFull(idx),
                provider.GetPinyinInitials(idx),
                query.keywords);
            if (score > 0) {
                MatchResult mr;
                mr.provider = &provider;
                mr.entryIndex = idx;
                mr.rawScore = score;
                results.push_back(mr);
            }
        }
        return results;
    }

    // 无 TrigramIndex 或短关键词：走前缀扫描（兼容 App/Bookmark）
    const std::size_t startIdx = provider.FindFirstPrefix(query.keywords[0]);
    const std::size_t count = provider.GetCount();

    const bool prefixScan = true;
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
            lastWasPrefixHit = StartsWithCI(item.title, query.keywords[0]);
        } else if (prefixScan) {
            if (lastWasPrefixHit) {
                const std::wstring_view name = item.title;
                if (!name.empty() && !StartsWithCI(name, query.keywords[0])) {
                    break;
                }
            }
        }
    }
    return results;
}

} // namespace iris
