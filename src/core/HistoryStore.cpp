// Iris Core —— HistoryStore 实现（SQLite）
#include "core/HistoryStore.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "provider/IProvider.h"

#include <sqlite3.h>

#include <chrono>
#include <ctime>

namespace iris {

namespace {
constexpr const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS history (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT    NOT NULL,
    path       TEXT    NOT NULL,
    type       INTEGER NOT NULL,
    open_count INTEGER NOT NULL DEFAULT 1,
    first_open INTEGER NOT NULL,
    last_open  INTEGER NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_history_path ON history(path);
CREATE INDEX        IF NOT EXISTS idx_history_last_open ON history(last_open DESC);
)SQL";

std::int64_t NowUnix() {
    return static_cast<std::int64_t>(std::time(nullptr));
}
} // namespace

HistoryStore::HistoryStore(const std::filesystem::path& dbPath) {
    const std::string utf8Path = StringUtil::WideToUtf8(dbPath.wstring());
    if (sqlite3_open(utf8Path.c_str(), &db_) != SQLITE_OK) {
        IRIS_LOG_WARN(L"HistoryStore 打开失败: " + dbPath.wstring());
        db_ = nullptr;
        return;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, kSchema, nullptr, nullptr, nullptr);
}

HistoryStore::~HistoryStore() {
    if (db_) sqlite3_close(db_);
}

void HistoryStore::RecordOpen(const ResultItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    const std::string title = StringUtil::WideToUtf8(item.title);
    const std::string path  = StringUtil::WideToUtf8(item.path);
    const int type = static_cast<int>(item.type);
    const std::int64_t now = NowUnix();

    const char* sql =
        "INSERT INTO history (title, path, type, open_count, first_open, last_open) "
        "VALUES (?,?,?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET open_count=open_count+1, last_open=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 3, type);
    sqlite3_bind_int64(stmt, 4, 1);
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int HistoryStore::GetOpenCount(const std::wstring& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;
    const std::string utf8Path = StringUtil::WideToUtf8(path);
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT open_count FROM history WHERE path=?", -1,
                           &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, utf8Path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

std::vector<ResultItem> HistoryStore::GetRecentItems(int limit) const {
    std::vector<ResultItem> out;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT title, path, type, open_count FROM history "
            "ORDER BY last_open DESC LIMIT ?", -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ResultItem item;
        item.title = StringUtil::Utf8ToWide(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        item.path = StringUtil::Utf8ToWide(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        item.type = static_cast<ItemType>(sqlite3_column_int(stmt, 2));
        item.openCount = sqlite3_column_int(stmt, 3);
        out.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    return out;
}

void HistoryStore::PruneOlderThan(int days) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    const std::int64_t cutoff = NowUnix() - static_cast<std::int64_t>(days) * 86400;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM history WHERE last_open<?", -1,
                           &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(stmt, 1, cutoff);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace iris
