// Iris 数据模型层 —— 紧凑索引存储
// 设计依据: doc/detailed-design.md §4.1.1
//
// 存储层使用紧凑二进制格式（对标 Everything）。所有字符串进 StringPool，
// 条目用 offset+length 引用。DirEntry=10B / CompactFileEntry=14B 需 pack(1)
// 才能达到标称大小（否则字段对齐会撑大）。
// 本文件为纯头文件（data-model 模块要求无 .cpp），方法全部 inline 于类体内，
// 可被多个 TU 安全包含。
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "provider/IProvider.h"

namespace iris {

// ── 字符串池：连续 wchar_t 缓冲，条目用 offset+length 引用 ──
class StringPool {
public:
    // 追加字符串，返回起始 offset（不查重，O(1)；调用方保证并发安全）
    uint32_t Insert(std::wstring_view str) {
        uint32_t offset = static_cast<uint32_t>(buffer_.size());
        buffer_.insert(buffer_.end(), str.begin(), str.end());
        return offset;
    }
    // 读取字符串（offset/length 由调用方持有）
    std::wstring_view Get(uint32_t offset, uint16_t length) const {
        return std::wstring_view(buffer_.data() + offset, length);
    }
    size_t SizeInBytes() const { return buffer_.size() * sizeof(wchar_t); }
    void Reserve(size_t chars) { buffer_.reserve(chars); }
    void Clear() { buffer_.clear(); }

private:
    std::vector<wchar_t> buffer_;
};

// ── 紧凑结构：pack(1) 以达到标称字节数 ──
#pragma pack(push, 1)

// 目录表条目：路径前缀去重，文件只引用 dirId
struct DirEntry {
    uint32_t pathOffset;     // 目录完整路径在 dirPool_ 中的 offset
    uint16_t pathLength;
    uint32_t parentDirId;    // 父目录 ID（UINT32_MAX = 根）
};
static_assert(sizeof(DirEntry) == 10, "DirEntry 必须 10 字节");

// 紧凑文件记录：仅存搜索/展示必需字段
struct CompactFileEntry {
    uint32_t nameOffset : 30;   // 文件名（纯名）在 namePool_ 的 offset
    uint32_t type       : 2;    // ItemType: FILE=0, BOOKMARK=1, APPLICATION=2
    uint16_t nameLength;        // 文件名长度（wchar_t 计数）
    uint16_t dirId;             // 目录表索引
    uint8_t  pathDepth;         // 路径深度（0-255）
    uint8_t  flags;             // bit0:隐藏 bit1:系统 bit2:目录
    uint32_t usnLow;            // USN 低 32 位（增量更新用）
    // 注：openCount 不在索引中 —— Ranker 从 HistoryStore 实时查
};
static_assert(sizeof(CompactFileEntry) == 14, "CompactFileEntry 必须 14 字节");

#pragma pack(pop)

// ── 索引存储（所有 Provider 统一使用）──
class IndexStore {
public:
    void Reserve(size_t n) { entries_.reserve(n); }

    // 追加一条（不保持有序；批量插入后调用 Sort()）
    void Insert(CompactFileEntry entry) { entries_.push_back(entry); }
    // 新增目录，返回 dirId
    uint16_t AddDir(DirEntry dir) {
        uint16_t id = static_cast<uint16_t>(dirs_.size());
        dirs_.push_back(dir);
        return id;
    }

    // 按文件名字典序排序（排序后支持二分查找与前缀压缩）
    void Sort() {
        std::sort(entries_.begin(), entries_.end(),
                  [this](const CompactFileEntry& a, const CompactFileEntry& b) {
                      return NameAt(a) < NameAt(b);
                  });
    }

    // 二分查找前缀起始下标：首个 name >= prefix 的位置（调用方再向前缀扫描）
    size_t FindFirstPrefix(std::wstring_view prefix) const {
        if (entries_.empty() || prefix.empty()) return 0;
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), prefix,
            [this](const CompactFileEntry& e, std::wstring_view p) {
                return NameAt(e) < p;
            });
        return static_cast<size_t>(it - entries_.begin());
    }

    const CompactFileEntry& operator[](size_t i) const { return entries_[i]; }
    const DirEntry& Dir(size_t dirId) const { return dirs_[dirId]; }
    size_t size() const { return entries_.size(); }
    size_t DirCount() const { return dirs_.size(); }

    // —— 按需字段访问（Matcher/Ranker 通过 Provider 间接调用）——
    std::wstring_view NameAt(const CompactFileEntry& e) const {
        return namePool_.Get(e.nameOffset, e.nameLength);
    }
    std::wstring_view DirAt(const DirEntry& d) const {
        return dirPool_.Get(d.pathOffset, d.pathLength);
    }
    std::wstring_view DirOf(const CompactFileEntry& e) const {
        return DirAt(dirs_[e.dirId]);
    }

    StringPool& NamePool() { return namePool_; }
    StringPool& DirPool() { return dirPool_; }

    // 重建 ResultItem（文件语义：title=文件名，subtitle=目录路径，path=完整路径）
    // 书签/应用由各自 Provider 的 BuildResultItem 定制，可不复用此实现。
    ResultItem ToResultItem(const CompactFileEntry& e) const {
        ResultItem item;
        auto name = NameAt(e);
        auto dir  = DirOf(e);
        item.title    = std::wstring(name);
        item.subtitle = std::wstring(dir);
        item.path     = std::wstring(dir);
        if (!item.path.empty() && item.path.back() != L'\\' && item.path.back() != L'/')
            item.path += L'\\';
        item.path += std::wstring(name);
        item.type      = static_cast<ItemType>(e.type);
        item.pathDepth = e.pathDepth;
        return item;
    }

    size_t TotalMemoryBytes() const {
        return entries_.capacity() * sizeof(CompactFileEntry)
             + dirs_.capacity() * sizeof(DirEntry)
             + namePool_.SizeInBytes() + dirPool_.SizeInBytes();
    }

private:
    std::vector<CompactFileEntry> entries_;  // 按文件名字典序排列（Sort 后）
    std::vector<DirEntry>         dirs_;
    StringPool                    namePool_;
    StringPool                    dirPool_;
};

} // namespace iris
