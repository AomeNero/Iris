// Iris UI —— 图标缓存（混合 key + LRU 上限）
// key 策略：
//   图标类扩展名（exe/lnk/ico/msi/scr，内嵌各自真实图标）→ 按完整路径缓存
//     （大小写无关；LRU 上限兜底内存，~8MB @ 512 条 64px）；
//   其余扩展名 → 系统按扩展名/属性给通用图标（SHGFI_USEFILEATTRIBUTES 语义），
//     按扩展名共享一条缓存（条目数以扩展名种数为上界，内存优化约束不回退）。
// loader 返回结果经调用方缩放后存储（不驻留 JUMBO 256px 原图）。
#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <QPixmap>

#include "core/LruCache.h"

namespace iris {

class IconCache {
public:
    using Loader = std::function<QPixmap(const std::wstring& path)>;

    explicit IconCache(Loader loader, size_t capacity = 512);

    // 取图标；空路径直接返回空。加载失败（空 pixmap）同样缓存，防反复失败。
    QPixmap Get(const std::wstring& path);

    size_t Size() const { return cache_.Size(); }

private:
    Loader loader_;
    LruCache<std::wstring, QPixmap> cache_;
};

// 路径 → 缓存 key：图标类扩展名返回小写全路径，其余返回小写扩展名；
// 无扩展名返回空串共享兜底（目录部分的点不算扩展名分隔）。
std::wstring IconKey(const std::wstring& path);

// 路径的扩展名是否内嵌真实图标（exe/lnk/ico/msi/scr）——
// 决定缓存 key 策略与 loader 的提取模式（真实文件 vs 属性推断）。
bool HasIconBearingExtension(const std::wstring& path);

} // namespace iris
