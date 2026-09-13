// Iris Provider —— 全量重建熔断器实现
#include "provider/RebuildBreaker.h"

namespace iris {

RebuildBreaker::RebuildBreaker(size_t maxRebuilds, uint64_t windowMs)
    : maxRebuilds_(maxRebuilds), windowMs_(windowMs) {}

void RebuildBreaker::Purge(uint64_t nowMs) {
    while (!rebuildMs_.empty() && nowMs - rebuildMs_.front() >= windowMs_)
        rebuildMs_.pop_front();
}

void RebuildBreaker::RecordRebuild(uint64_t nowMs) {
    Purge(nowMs);
    rebuildMs_.push_back(nowMs);
}

bool RebuildBreaker::ShouldAllow(uint64_t nowMs) {
    Purge(nowMs);
    return rebuildMs_.size() < maxRebuilds_;
}

} // namespace iris
