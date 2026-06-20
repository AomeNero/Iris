// Iris Core —— 使用历史记录存储（SQLite）
// 设计依据: doc/detailed-design.md §9.1
// 前移自 P1 模块7：Ranker 排序需实时查询 openCount，故提前实现。
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace iris {

struct ResultItem;

class HistoryStore {
public:
    /// dbPath 可传 L":memory:" 做内存库（用于测试）。
    explicit HistoryStore(const std::filesystem::path& dbPath);
    ~HistoryStore();

    HistoryStore(const HistoryStore&)            = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    /// 记录一次打开（upsert：已存在则 open_count+1，否则插入）
    void RecordOpen(const ResultItem& item);

    /// 获取单个路径的打开次数（Ranker 按需调用）
    int GetOpenCount(const std::wstring& path) const;

    /// 最近使用的 N 条记录（用于弹出时显示）
    std::vector<ResultItem> GetRecentItems(int limit = 9) const;

    /// 清理超过 N 天的历史记录
    void PruneOlderThan(int days = 90);

    bool IsOpen() const { return db_ != nullptr; }

private:
    sqlite3*      db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace iris
