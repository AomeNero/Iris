// Iris Core —— 配置管理实现
#include "core/Config.h"

#include <nlohmann/json.hpp>

#include "core/WinUtil.h"  // GetExeDir（便携式数据目录）

#include <fstream>
#include <memory>

namespace iris {

namespace {
std::filesystem::path g_dataDirOverride;  // 仅测试用（Config::SetDataDirForTest）
} // namespace

void Config::SetDataDirForTest(const std::filesystem::path& dir) { g_dataDirOverride = dir; }

Config& Config::Instance() {
    static Config inst;
    return inst;
}

std::filesystem::path Config::GetDataDir() const {
    if (!g_dataDirOverride.empty()) return g_dataDirOverride;
    return std::filesystem::path(WinUtil::GetExeDir());  // 便携式：数据随 Iris.exe
}
std::filesystem::path Config::GetConfigPath() const  { return GetDataDir() / L"config.json"; }
std::filesystem::path Config::GetHistoryPath() const { return GetDataDir() / L"history.db"; }
std::filesystem::path Config::GetLogPath() const     { return GetDataDir() / L"iris.log"; }

Config::AppConfig Config::Load() {
    const auto path = GetConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    AppConfig cfg;  // 默认值
    std::ifstream in(path);
    if (in) {
        try {
            nlohmann::json j;
            in >> j;
            cfg.hotkey        = j.value("hotkey", cfg.hotkey);          // 字符串，如 "Alt+Space"
            cfg.theme         = j.value("theme", cfg.theme);            // "light" / "dark"
            cfg.maxResults    = j.value("max_results", cfg.maxResults);
            cfg.autoStart     = j.value("auto_start", cfg.autoStart);
            cfg.excludeHidden = j.value("exclude_hidden", cfg.excludeHidden);
            cfg.excludeSystem = j.value("exclude_system", cfg.excludeSystem);
            if (j.contains("providers") && j["providers"].is_object()) {
                const auto& pr = j["providers"];
                if (pr.contains("file"))     cfg.providers.file.enabled     = pr["file"].value("enabled", true);
                if (pr.contains("bookmark")) cfg.providers.bookmark.enabled = pr["bookmark"].value("enabled", true);
                if (pr.contains("app"))      cfg.providers.app.enabled      = pr["app"].value("enabled", true);
            }
        } catch (...) {
            cfg = AppConfig{};  // 损坏的配置 → 回退默认
        }
    }
    {
        std::unique_lock lock(mutex_);
        config_ = cfg;
    }
    return cfg;
}

void Config::Save() {
    AppConfig cfg;
    {
        std::shared_lock lock(mutex_);  // 内部读锁读取 config_
        cfg = config_;
    }
    const auto path = GetConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    nlohmann::json j;
    j["hotkey"]         = cfg.hotkey;
    j["theme"]          = cfg.theme;
    j["max_results"]    = cfg.maxResults;
    j["auto_start"]     = cfg.autoStart;
    j["exclude_hidden"] = cfg.excludeHidden;
    j["exclude_system"] = cfg.excludeSystem;
    j["providers"] = {
        {"file", {{"enabled", cfg.providers.file.enabled}}},
        {"bookmark", {{"enabled", cfg.providers.bookmark.enabled}}},
        {"app", {{"enabled", cfg.providers.app.enabled}}},
    };

    std::ofstream out(path);
    if (out) out << j.dump(2);
}

Config::AppConfig Config::Get() const {
    std::shared_lock lock(mutex_);
    return config_;
}

void Config::Set(const AppConfig& cfg) {
    std::unique_lock lock(mutex_);
    config_ = cfg;
}

} // namespace iris
