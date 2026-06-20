// 模块 3 测试：Config Load/Save
// 设计依据: doc/code-prompt.md §六 Module3 / doc/detailed-design.md §5.1
#include <gtest/gtest.h>

#include "core/Config.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using iris::Config;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_ = fs::temp_directory_path() / L"iris_config_test";
        std::error_code ec;
        fs::remove_all(tmp_, ec);
        fs::create_directories(tmp_, ec);
        Config::Instance().SetDataDirForTest(tmp_);
    }
    void TearDown() override {
        Config::Instance().SetDataDirForTest({});
        std::error_code ec;
        fs::remove_all(tmp_, ec);
    }
    fs::path tmp_;
};

// Load 不存在的文件 → 返回默认配置
TEST_F(ConfigTest, LoadMissingReturnsDefaults) {
    const auto cfg = Config::Instance().Load();
    EXPECT_EQ(cfg.maxResults, 9);
    EXPECT_TRUE(cfg.autoStart);
    EXPECT_TRUE(cfg.excludeHidden);
    EXPECT_TRUE(cfg.excludeSystem);
    EXPECT_EQ(cfg.hotkey, "Alt+Space");
    EXPECT_EQ(cfg.theme, "light");
    EXPECT_TRUE(cfg.providers.file.enabled);
    EXPECT_TRUE(cfg.providers.bookmark.enabled);
    EXPECT_TRUE(cfg.providers.app.enabled);
}

// Save 后 Load → 数据一致（往返）
TEST_F(ConfigTest, SaveThenLoadRoundTrip) {
    Config::AppConfig cfg = Config::Instance().Load();  // defaults
    cfg.maxResults = 5;
    cfg.autoStart  = false;
    cfg.hotkey = "Alt+A";  // 'A'
    cfg.providers.bookmark.enabled = false;

    Config::Instance().Set(cfg);
    Config::Instance().Save();

    const Config::AppConfig loaded = Config::Instance().Load();  // 从磁盘重读
    EXPECT_EQ(loaded.maxResults, 5);
    EXPECT_FALSE(loaded.autoStart);
    EXPECT_EQ(loaded.hotkey, "Alt+A");
    EXPECT_FALSE(loaded.providers.bookmark.enabled);
    EXPECT_TRUE(loaded.providers.file.enabled);   // 未改动项保持默认
    EXPECT_TRUE(loaded.providers.app.enabled);
}

// 损坏的 JSON → 回退默认（不崩溃）
TEST_F(ConfigTest, CorruptFileFallsBackToDefaults) {
    {
        std::ofstream out(tmp_ / L"config.json");
        out << "{ this is : not valid json }}}";
    }
    const auto cfg = Config::Instance().Load();
    EXPECT_EQ(cfg.maxResults, 9);  // 默认值
}
