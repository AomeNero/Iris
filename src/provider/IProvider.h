// Iris 数据模型层 —— 统一数据源接口与结果传输对象
// 设计依据: doc/detailed-design.md §4.1.2, §4.2.1
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QObject>
#include <QVector>

namespace iris {

// 结果类型枚举（位宽 2，与 CompactFileEntry::type 位域匹配）
enum class ItemType : uint8_t {
    FILE        = 0,
    BOOKMARK    = 1,
    APPLICATION = 2,
};

// ── 传输层：ResultItem ──
// 仅作为 Engine↔UI 的搜索结果传输载体（最多 9 条），不是存储格式。
// 设计约束 [detailed-design §4.1.2]:
//   - 不存储 HICON —— UI 层通过 SHGetFileInfoW 按需加载并缓存 9 条可见项
//   - 不存储 searchText —— 匹配时对原始字符串做 case-insensitive 比较
struct ResultItem {
    std::wstring title;        // 显示名称（文件名/书签名/应用名）
    std::wstring subtitle;     // 补充信息（路径/URL/文件夹层级）
    std::wstring path;         // 完整路径或 URL（用于打开操作）
    ItemType     type     = ItemType::FILE;
    int          score    = 0;     // 搜索得分（Engine 填入）
    int          openCount = 0;
    uint32_t     pathDepth = 0;
};
static_assert(sizeof(ResultItem) <= 200, "ResultItem 过大，仅 9 条无需极致压缩");

using ResultList = std::vector<ResultItem>;  // 最多 9 条

// ── 基础接口：IProvider ──
// 所有数据源实现此接口。生命周期 + 数据获取。
// [detailed-design §4.2.1]
class IProvider : public QObject {
    Q_OBJECT
public:
    explicit IProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IProvider() override = default;

    // ---- 生命周期 ----
    /// 初始化数据源（可能耗时，在索引线程中调用）
    virtual bool Initialize() = 0;
    /// 关闭数据源，释放资源
    virtual void Shutdown() = 0;

    // ---- 数据获取 ----
    /// 返回全量结果（仅 Engine 初始化时调用一次，非搜索路径）
    virtual std::vector<ResultItem> GetAll() const = 0;
    /// 返回结果总数（用于性能监控）
    virtual size_t GetCount() const = 0;

    // ---- 元信息 ----
    virtual std::wstring GetName() const = 0;
    /// 数据源是否就绪（初始化完成后返回 true）
    virtual bool IsReady() const = 0;

    // ---- 刷新 ----
    /// 触发增量刷新（在索引线程中周期性调用）
    virtual void Refresh() = 0;

signals:
    /// 数据发生变化时发出（Engine 收到后无需重建索引，Provider 内部已 CoW 更新）
    void dataChanged();
};

} // namespace iris
