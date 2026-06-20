// Iris Engine —— 搜索引擎协调器
// 设计依据: doc/detailed-design.md §7.5, doc/code-prompt.md §六 Module5
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QObject>
#include <QVector>

#include "core/HistoryStore.h"
#include "core/ThreadPool.h"
#include "engine/Matcher.h"
#include "engine/QueryParser.h"
#include "engine/Ranker.h"
#include "provider/ISearchableProvider.h"
#include "provider/IProvider.h"

namespace iris {

class SearchEngine : public QObject {
    Q_OBJECT
public:
    explicit SearchEngine(QObject* parent = nullptr);
    ~SearchEngine() override;

    void SetProviders(std::shared_ptr<ISearchableProvider> file,
                      std::shared_ptr<ISearchableProvider> bookmark,
                      std::shared_ptr<ISearchableProvider> app);
    void SetHistoryStore(HistoryStore* store);
    /// 设置单次搜索返回的最大结果数（默认 9）。
    void SetMaxResults(int n) { maxResults_ = n; }

    /// 异步搜索（内部自动取消上一次）。返回 requestId。
    int SearchAsync(const std::wstring& rawText);
    /// 取消当前搜索
    void CancelSearch();

signals:
    /// 搜索完成（在 UI 线程经 QueuedConnection 接收）
    void searchFinished(QVector<ResultItem> results);

private:
    struct SearchContext {
        int                                 requestId = 0;
        std::wstring                        rawText;
        std::shared_ptr<std::atomic<bool>>  cancelled;
    };

    void DoSearch(std::shared_ptr<SearchContext> ctx);

    QueryParser  parser_;
    Matcher      matcher_;
    Ranker       ranker_;

    std::shared_ptr<ISearchableProvider> fileProvider_;
    std::shared_ptr<ISearchableProvider> bookmarkProvider_;
    std::shared_ptr<ISearchableProvider> appProvider_;
    HistoryStore* historyStore_ = nullptr;
    int           maxResults_   = 9;

    std::shared_ptr<std::atomic<bool>> cancelFlag_;
    std::mutex                          cancelMutex_;
    std::atomic<int>                    nextRequestId_{0};
    ThreadPool                          threadPool_{4};
};

} // namespace iris
