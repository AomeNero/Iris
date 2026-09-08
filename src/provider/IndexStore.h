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
#include "provider/TrigramIndex.h"
#include "provider/MappedSnapshot.h"

namespace iris {

class IndexSnapshot;  // 序列化（friend：直接读写私有容器做紧凑布局直拷）

// ── 字符串池：连续 wchar_t 缓冲，条目用 offset+length 引用 ──
// 双后端：堆缓冲（构建期）或外部视图（快照映射加载，AttachView——
// 字符串本体留在文件映射中，仅持指针，读取结果一致）。
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
        if (length == 0) return {};
        const wchar_t* base = !buffer_.empty() ? buffer_.data() : viewBuf_;
        return std::wstring_view(base + offset, length);
    }
    size_t SizeInBytes() const { return buffer_.size() * sizeof(wchar_t); }
    const wchar_t* Data() const { return buffer_.data(); }
    void Reserve(size_t chars) { buffer_.reserve(chars); }
    void Clear() { buffer_.clear(); }
    // 反序列化专用：整体替换缓冲（IndexSnapshot::Load 使用）
    void AssignRaw(const wchar_t* data, size_t chars) {
        buffer_.assign(data, data + chars);
    }
    // 视图模式：字符串本体在外部存储（文件映射）
    void AttachView(const wchar_t* data) { viewBuf_ = data; }

private:
    std::vector<wchar_t> buffer_;
    const wchar_t* viewBuf_ = nullptr;
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
    friend class IndexSnapshot;  // 快照序列化：紧凑布局直拷私有容器

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
        if (prefix.empty()) return 0;
        const size_t n = size();
        if (n == 0) return 0;
        const CompactFileEntry* first = IsView() ? view_.entries : entries_.data();
        const auto* it = std::lower_bound(
            first, first + n, prefix,
            [this](const CompactFileEntry& e, std::wstring_view p) {
                return NameAt(e) < p;
            });
        return static_cast<size_t>(it - first);
    }

    // —— 视图后端（快照映射加载）：数据在文件映射中，只读，写方法不可调用 ——
    bool IsView() const { return view_.entries != nullptr; }

    const CompactFileEntry& operator[](size_t i) const {
        return IsView() ? view_.entries[i] : entries_[i];
    }
    const DirEntry& Dir(size_t dirId) const {
        return IsView() ? view_.dirs[dirId] : dirs_[dirId];
    }
    size_t size() const { return IsView() ? view_.entryCount : entries_.size(); }
    size_t DirCount() const { return IsView() ? view_.dirCount : dirs_.size(); }

    // —— 按需字段访问（Matcher/Ranker 通过 Provider 间接调用）——
    std::wstring_view NameAt(const CompactFileEntry& e) const {
        return namePool_.Get(e.nameOffset, e.nameLength);
    }
    std::wstring_view DirAt(const DirEntry& d) const {
        return dirPool_.Get(d.pathOffset, d.pathLength);
    }
    std::wstring_view DirOf(const CompactFileEntry& e) const {
        return DirAt(Dir(e.dirId));  // 经 Dir() 分派（视图/堆双后端）
    }

    StringPool& NamePool() { return namePool_; }
    StringPool& DirPool() { return dirPool_; }

    // Trigram 倒排索引（Sort 后调用 BuildTrigramIndex）
    void BuildTrigramIndex() { trigramIndex_.Build(*this); }
    const TrigramIndex& GetTrigramIndex() const { return trigramIndex_; }

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
        if (IsView()) return 0;  // 数据在文件映射中，不占进程堆
        return entries_.capacity() * sizeof(CompactFileEntry)
             + dirs_.capacity() * sizeof(DirEntry)
             + namePool_.SizeInBytes() + dirPool_.SizeInBytes()
             + trigramIndex_.MemoryBytes();
    }

private:
    std::vector<CompactFileEntry> entries_;  // 按文件名字典序排列（Sort 后）
    std::vector<DirEntry>         dirs_;
    StringPool                    namePool_;
    StringPool                    dirPool_;
    TrigramIndex                  trigramIndex_;

    // 视图后端（快照映射加载；mapped_ 持有映射生命周期，随 store 析构释放）
    struct SnapshotView {
        const CompactFileEntry* entries    = nullptr;
        uint32_t                entryCount = 0;
        const DirEntry*         dirs       = nullptr;
        uint32_t                dirCount   = 0;
    };
    SnapshotView   view_;
    MappedSnapshot mapped_;
};

} // namespace iris
