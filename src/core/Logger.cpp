// Iris Core —— 日志系统实现（异步写入 + 按天轮转 + 保留 7 天）
#include "core/Logger.h"
#include "core/StringUtil.h"  // WideToUtf8

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace iris {

namespace {

struct Record {
    std::time_t time;
    LogLevel    level;
    std::string fileBase;  // 仅文件名，UTF-8
    int         line;
    std::string msg;       // UTF-8
};

struct State {
    std::filesystem::path logDir;
    LogLevel              minLevel = LogLevel::DEBUG;
    std::mutex            mu;
    std::condition_variable cv;
    std::deque<Record>    queue;
    std::thread           worker;
    std::atomic<bool>     running{false};
    std::atomic<bool>     initialized{false};
};

State& S() {
    static State s;
    return s;
}

const char* LevelName(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string BasenameUtf8(const char* file) {
    const char* p = file;
    for (const char* c = file; *c; ++c)
        if (*c == '\\' || *c == '/') p = c + 1;
    return std::string(p);
}

std::string TodayFileName() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "iris_%Y-%m-%d.log", &tm);
    return buf;
}

void RotateOldLogs(const std::filesystem::path& logDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (auto& e : fs::directory_iterator(logDir, ec)) {
        if (e.path().extension() != ".log") continue;
        auto wt = fs::last_write_time(e, ec);
        auto ageH = std::chrono::duration_cast<std::chrono::hours>(now - wt).count();
        if (ageH > 24 * 7) fs::remove(e.path(), ec);
    }
}

void WorkerMain() {
    auto& s = S();
    std::string currentDay;
    while (true) {
        std::vector<Record> batch;
        {
            std::unique_lock<std::mutex> lk(s.mu);
            s.cv.wait(lk, [&] { return !s.queue.empty() || !s.running.load(); });
            if (s.queue.empty() && !s.running.load()) break;
            batch.assign(std::make_move_iterator(s.queue.begin()),
                         std::make_move_iterator(s.queue.end()));
            s.queue.clear();
        }

        const std::string day = TodayFileName();
        if (day != currentDay) {
            currentDay = day;
            RotateOldLogs(s.logDir);
        }
        std::ofstream out(s.logDir / currentDay, std::ios::app);
        for (const auto& r : batch) {
            std::tm tm{};
            localtime_s(&tm, &r.time);
            char ts[32];
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
            out << '[' << ts << "] [" << LevelName(r.level) << "] ["
                << r.fileBase << ':' << r.line << "] " << r.msg << '\n';
        }
    }
}

} // namespace

void Logger::Init(const std::filesystem::path& logDir, LogLevel minLevel) {
    auto& s = S();
    if (s.initialized.exchange(true)) return;  // 已初始化
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    s.logDir   = logDir;
    s.minLevel = minLevel;
    s.running.store(true);
    s.worker = std::thread(WorkerMain);
}

void Logger::Shutdown() {
    auto& s = S();
    if (!s.initialized.exchange(false)) return;
    s.running.store(false);
    s.cv.notify_all();
    if (s.worker.joinable()) s.worker.join();
}

bool Logger::IsInitialized() { return S().initialized.load(); }

void Logger::Log(LogLevel level, const std::wstring& msg,
                 const char* file, int line, const char* /*func*/) {
    auto& s = S();
    if (!s.initialized.load() || static_cast<uint8_t>(level) < static_cast<uint8_t>(s.minLevel))
        return;
    Record r;
    r.time    = std::time(nullptr);
    r.level   = level;
    r.fileBase = BasenameUtf8(file);
    r.line    = line;
    r.msg     = StringUtil::WideToUtf8(msg);
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.queue.push_back(std::move(r));
    }
    s.cv.notify_one();
}

} // namespace iris
