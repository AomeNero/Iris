// Iris Core —— 配置管理（单例）
// 设计依据: doc/detailed-design.md §5.1
// 路径: %APPDATA%\IrisSearch\config.json  线程安全: shared_mutex
#pragma once

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>

namespace iris {

class Config {
public:
    struct ProviderConfig {
        bool enabled = true;
    };
    struct AppConfig {
        std::string hotkey     = "Alt+Space";  // 形如 "Alt+Space"，由 HotkeyManager::Parse 解析
        std::string theme      = "light";      // "light" / "dark"
        int  maxResults    = 9;
        bool autoStart     = true;
        bool excludeHidden = true;
        bool excludeSystem = true;
        struct {
            ProviderConfig file;
            ProviderConfig bookmark;
            ProviderConfig app;
        } providers;
    };

    static Config& Instance();

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    /// 读取磁盘配置（不存在则返回默认）。更新内部 config_。
    AppConfig Load();
    /// 保存当前内部 config_（无参数，内部加读锁读取后写盘）。
    void Save();
    /// 写入内存配置（加写锁；用于设置 UI 与测试）
    void Set(const AppConfig& cfg);

    /// 读取当前内存配置（加读锁）
    AppConfig Get() const;

    /// 仅测试用：覆盖数据目录（默认 %APPDATA%\IrisSearch）。传空路径恢复默认。
    static void SetDataDirForTest(const std::filesystem::path& dir);

    // ---- 路径 ----
    std::filesystem::path GetDataDir() const;       // %APPDATA%\IrisSearch
    std::filesystem::path GetConfigPath() const;    // ... config.json
    std::filesystem::path GetHistoryPath() const;   // ... history.db
    std::filesystem::path GetLogPath() const;       // ... iris.log (当天)

private:
    Config() = default;

    mutable std::shared_mutex mutex_;
    AppConfig config_;
};

} // namespace iris
