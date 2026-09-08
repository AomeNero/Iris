// Iris 数据模型层 —— 索引快照序列化
// 将 IndexStore（entries/dirs/namePool/dirPool/trigram CSR）以紧凑二进制布局
// 落盘，供启动时秒级加载（替代全盘重扫）。
//   Save：写 <path>.tmp → rename 原子替换（中断不留半成品）。
//   Load：严格校验 magic/版本/段总长==文件长，损坏一律返回 nullptr，
//         由调用方兜底全量重建（调用方须保证 store 已 Sort + BuildTrigramIndex）。
#pragma once

#include <filesystem>
#include <memory>

#include "provider/IndexStore.h"

namespace iris {

class IndexSnapshot {
public:
    /// 序列化并原子落盘。成功返回 true（失败不触碰既有快照）。
    static bool Save(const IndexStore& store, const std::filesystem::path& path);

    /// 加载快照（拷入堆）；缺失/损坏/版本不符返回 nullptr。
    static std::unique_ptr<IndexStore> Load(const std::filesystem::path& path);

    /// 加载快照为映射视图：数据留在文件映射（页缓存）中，不拷入堆，
    /// 专用内存近乎零增量；返回的 IndexStore 持有映射生命周期。
    /// 缺失/损坏/版本不符返回 nullptr。
    static std::unique_ptr<IndexStore> LoadMapped(const std::filesystem::path& path);
};

} // namespace iris
