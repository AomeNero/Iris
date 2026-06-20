// Iris Core —— 日志系统（异步写入，按天轮转，保留 7 天）
// 设计依据: doc/detailed-design.md §5.2
// 格式: [2026-06-19 14:30:00] [INFO] [file.cpp:42] msg
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace iris {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    Error = 3,   // 不用全大写 ERROR：避免与 windows.h 的 ERROR 宏冲突
};

class Logger {
public:
    /// 初始化日志系统（启动后台写入线程，按天轮转）
    static void Init(const std::filesystem::path& logDir, LogLevel minLevel = LogLevel::DEBUG);
    /// 刷新并关闭日志系统（drain 队列后 join 线程）
    static void Shutdown();
    static bool IsInitialized();

    /// 提交一条日志（线程安全；若未初始化则忽略）
    static void Log(LogLevel level, const std::wstring& msg,
                    const char* file, int line, const char* func);
};

} // namespace iris

#define IRIS_LOG(level, msg) \
    ::iris::Logger::Log(level, (msg), __FILE__, __LINE__, __FUNCTION__)

#define IRIS_LOG_DEBUG(msg) IRIS_LOG(::iris::LogLevel::DEBUG, (msg))
#define IRIS_LOG_INFO(msg)  IRIS_LOG(::iris::LogLevel::INFO,  (msg))
#define IRIS_LOG_WARN(msg)  IRIS_LOG(::iris::LogLevel::WARN,  (msg))
#define IRIS_LOG_ERROR(msg) IRIS_LOG(::iris::LogLevel::Error, (msg))
