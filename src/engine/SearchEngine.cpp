// Iris Engine —— SearchEngine 实现
#include "engine/SearchEngine.h"

#include "core/Logger.h"

#include <chrono>

namespace iris {

SearchEngine::SearchEngine(QObject* parent) : QObject(parent) {
    cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    searchThread_ = std::thread([this]() { SearchThreadLoop(); });
}

SearchEngine::~SearchEngine() {
    CancelSearch();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopThread_ = true;
    }
    queueCv_.notify_one();
    if (searchThread_.joinable()) searchThread_.join();
}

void SearchEngine::SetProviders(std::shared_ptr<ISearchableProvider> file,
                                std::shared_ptr<ISearchableProvider> bookmark,
                                std::shared_ptr<ISearchableProvider> app) {
    fileProvider_     = std::move(file);
    bookmarkProvider_ = std::move(bookmark);
    appProvider_      = std::move(app);
}

void SearchEngine::SetHistoryStore(HistoryStore* store) { historyStore_ = store; }

void SearchEngine::CancelSearch() {
    std::lock_guard<std::mutex> lock(cancelMutex_);
    if (cancelFlag_) cancelFlag_->store(true, std::memory_order_release);
    cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
}

int SearchEngine::SearchAsync(const std::wstring& rawText) {
    CancelSearch();  // 取消上一次

    const int id = nextRequestId_.fetch_add(1);
    auto ctx = std::make_shared<SearchContext>();
    ctx->requestId = id;
    ctx->rawText   = rawText;
    {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        ctx->cancelled = cancelFlag_;
    }

    // 推入队列，唤醒专用搜索线程（~0.05ms 唤醒延迟）
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        requestQueue_.push(ctx);
    }
    queueCv_.notify_one();
    return id;
}

void SearchEngine::DoSearch(std::shared_ptr<SearchContext> ctx) {
    if (ctx->cancelled->load(std::memory_order_acquire)) return;

    const auto searchStart = std::chrono::steady_clock::now();

    const ParsedQuery query = parser_.Parse(ctx->rawText);

    // 空输入 → 空结果 [已确认 E1]
    if (query.isEmpty()) {
        if (!ctx->cancelled->load(std::memory_order_acquire))
            emit searchFinished({});
        return;
    }

    // 按类型过滤选择 Provider [已确认 E2]
    std::vector<ISearchableProvider*> providers;
    if (query.filterType == ItemType::APPLICATION) {
        if (appProvider_)      providers.push_back(appProvider_.get());
    } else if (query.filterType == ItemType::BOOKMARK) {
        if (bookmarkProvider_) providers.push_back(bookmarkProvider_.get());
    } else {
        if (fileProvider_)     providers.push_back(fileProvider_.get());
        if (bookmarkProvider_) providers.push_back(bookmarkProvider_.get());
        if (appProvider_)      providers.push_back(appProvider_.get());
    }

    std::vector<MatchResult> matches;
    for (auto* p : providers) {
        if (!p || !p->IsReady()) continue;
        if (ctx->cancelled->load(std::memory_order_acquire)) return;
        auto pm = matcher_.Match(*p, query, *ctx->cancelled);
        matches.insert(matches.end(),
                       std::make_move_iterator(pm.begin()),
                       std::make_move_iterator(pm.end()));
    }

    if (ctx->cancelled->load(std::memory_order_acquire)) return;

    auto results = ranker_.Rank(matches, historyStore_, maxResults_);

    if (ctx->cancelled->load(std::memory_order_acquire)) return;

    QVector<ResultItem> qresults;
    qresults.reserve(static_cast<int>(results.size()));
    for (auto& r : results)
        qresults.append(std::move(r));

    IRIS_LOG_INFO(L"Search 耗时=" +
                  std::to_wstring(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - searchStart)
                                      .count()) +
                  L"us 关键词=" + ctx->rawText + L" 匹配=" +
                  std::to_wstring(matches.size()));
    emit searchFinished(qresults);
}

void SearchEngine::SearchThreadLoop() {
    while (true) {
        std::shared_ptr<SearchContext> ctx;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return stopThread_.load() || !requestQueue_.empty();
            });
            if (stopThread_.load() && requestQueue_.empty()) return;
            ctx = std::move(requestQueue_.front());
            requestQueue_.pop();
        }
        DoSearch(ctx);
    }
}

} // namespace iris
