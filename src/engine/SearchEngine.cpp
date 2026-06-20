// Iris Engine —— SearchEngine 实现
#include "engine/SearchEngine.h"

#include "core/Logger.h"

namespace iris {

SearchEngine::SearchEngine(QObject* parent) : QObject(parent) {
    cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
}

SearchEngine::~SearchEngine() {
    CancelSearch();
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

    // 值捕获 shared_ptr，跨线程安全；禁止 [&]
    threadPool_.Enqueue([this, ctx]() { DoSearch(ctx); });
    return id;
}

void SearchEngine::DoSearch(std::shared_ptr<SearchContext> ctx) {
    if (ctx->cancelled->load(std::memory_order_acquire)) return;

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

    emit searchFinished(qresults);
}

} // namespace iris
