// 模块 5 测试：SearchEngine（异步/取消/空输入/连续搜索）
// 设计依据: doc/detailed-design.md §7.5
//
// 搜索在 SearchEngine 自有线程池执行，结果经 QueuedConnection 信号回到主线程，
// 故测试需运行 Qt 事件循环。本文件提供自定义 main 创建 QCoreApplication。
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <memory>

#include "core/HistoryStore.h"
#include "engine/SearchEngine.h"
#include "provider/IProvider.h"
#include "mock_provider.h"

using iris::SearchEngine;
using iris::ResultItem;

namespace {
// 发起搜索并等待信号（带 3s 超时）
QVector<ResultItem> RunSearch(SearchEngine& eng, const std::wstring& q) {
    QVector<ResultItem> out;
    bool got = false;
    QEventLoop loop;
    QObject::connect(&eng, &SearchEngine::searchFinished,
                     [&](QVector<ResultItem> r) { out = std::move(r); got = true; loop.quit(); });
    eng.SearchAsync(q);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    EXPECT_TRUE(got) << "searchFinished 未在超时内到达";
    return out;
}

std::shared_ptr<MockProvider> MakeProvider(std::vector<MockProvider::Entry> es) {
    return std::make_shared<MockProvider>(std::move(es));
}
} // namespace

TEST(SearchEngineTest, EmptyInputReturnsEmpty) {
    SearchEngine eng;
    eng.SetProviders(nullptr, nullptr, nullptr);
    auto r = RunSearch(eng, L"");
    EXPECT_TRUE(r.isEmpty());
}

TEST(SearchEngineTest, ReturnsMatchingResultsAcrossProviders) {
    auto file = MakeProvider({
        {L"report.docx",  L"D:\\docs", L"D:\\docs\\report.docx",  iris::ItemType::FILE, 1},
    });
    auto app = MakeProvider({
        {L"ReportTool.exe", L"C:\\apps", L"C:\\apps\\ReportTool.exe", iris::ItemType::APPLICATION, 0},
    });
    SearchEngine eng;
    eng.SetProviders(file, nullptr, app);
    auto r = RunSearch(eng, L"report");
    EXPECT_GE(r.size(), 2);  // 文件 + 应用都命中
}

TEST(SearchEngineTest, FilterByType) {
    auto file = MakeProvider({{L"report.docx", L"d", L"d\\report.docx", iris::ItemType::FILE, 1}});
    auto app  = MakeProvider({{L"ReportApp",  L"a", L"a\\ReportApp",  iris::ItemType::APPLICATION, 0}});
    SearchEngine eng;
    eng.SetProviders(file, nullptr, app);
    auto r = RunSearch(eng, L"#report");  // 仅应用
    ASSERT_FALSE(r.isEmpty());
    for (const auto& item : r)
        EXPECT_EQ(item.type, iris::ItemType::APPLICATION);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
