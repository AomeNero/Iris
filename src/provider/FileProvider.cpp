// Iris Provider —— 文件索引（NTFS USN Journal）实现
// 设计依据: doc/detailed-design.md §6.1, doc/code-prompt.md §六 Module4
#include "provider/FileProvider.h"

#include "core/Logger.h"
#include "core/StringUtil.h"
#include "core/WinUtil.h"
#include "provider/IndexSnapshot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <cwctype>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

namespace iris {

namespace {

// 排除的扩展名（constexpr std::array + std::find）
constexpr std::array kExcludedExtensions = {
    L".tmp",  L".temp", L".cache", L".log",
    L".sys",  L".dll",  L".ocx",   L".drv",
};

// 临时条目：全量枚举后建索引前的中间结构
struct TempEntry {
    std::wstring name;
    uint64_t     frn        = 0;
    uint64_t     parentFrn  = 0;
    DWORD        attributes = 0;
};

// 解析单个 USN_RECORD，写入 TempEntry。返回记录长度（0 表示结束/无效）。
DWORD ParseUsnRecord(const USN_RECORD* rec, TempEntry& out) {
    if (rec->RecordLength == 0) return 0;
    out.frn       = rec->FileReferenceNumber;
    out.parentFrn = rec->ParentFileReferenceNumber;
    out.attributes = rec->FileAttributes;
    const wchar_t* name = reinterpret_cast<const wchar_t*>(
        reinterpret_cast<const BYTE*>(rec) + rec->FileNameOffset);
    out.name.assign(name, rec->FileNameLength / sizeof(wchar_t));
    return rec->RecordLength;
}

// 由 FRN 链重建目录完整路径（带缓存）。返回路径与深度。
std::wstring BuildDirPath(uint64_t dirFrn,
                          const std::unordered_map<uint64_t, TempEntry>& all,
                          const std::wstring& rootPath,
                          std::unordered_map<uint64_t, std::wstring>& cache,
                          uint8_t& depthOut) {
    auto it = cache.find(dirFrn);
    if (it != cache.end()) return it->second;

    // 向上收集祖先
    std::vector<uint64_t> chain;
    uint64_t cur = dirFrn;
    uint8_t depth = 0;
    while (true) {
        auto eit = all.find(cur);
        if (eit == all.end()) break;
        if (eit->second.parentFrn == cur) break;  // 根目录自引用
        chain.push_back(cur);
        cur = eit->second.parentFrn;
        ++depth;
    }
    // 从根向下拼接
    std::wstring path = rootPath;
    for (auto it2 = chain.rbegin(); it2 != chain.rend(); ++it2) {
        auto eit = all.find(*it2);
        if (eit == all.end()) break;
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        path += eit->second.name;
    }
    depthOut = depth;
    cache[dirFrn] = path;
    return path;
}

} // namespace

// ============================================================================
// 排除规则 [已确认 — B2]
// ============================================================================
bool FileProvider::ShouldExclude(DWORD fileAttributes, const std::wstring& fileName,
                                 bool excludeHidden, bool excludeSystem) {
    if (excludeHidden && (fileAttributes & FILE_ATTRIBUTE_HIDDEN)) return true;
    if (excludeSystem && (fileAttributes & FILE_ATTRIBUTE_SYSTEM)) return true;
    const std::wstring ext = StringUtil::ExtractExtension(fileName);
    if (std::find(kExcludedExtensions.begin(), kExcludedExtensions.end(), ext)
        != kExcludedExtensions.end())
        return true;
    return false;
}

void FileProvider::SetExcludeFlags(bool excludeHidden, bool excludeSystem) {
    excludeHidden_ = excludeHidden;
    excludeSystem_ = excludeSystem;
}

// ============================================================================
// 构造 / 析构
// ============================================================================
FileProvider::FileProvider()  = default;
FileProvider::~FileProvider() { Shutdown(); }

// ============================================================================
// Initialize：枚举卷 → 全量 USN 索引 → CoW 原子替换
// ============================================================================
void FileProvider::CancelInitialize() {
    cancelInitialize_.store(true, std::memory_order_release);
}

bool FileProvider::Initialize() {
    const auto initStart = std::chrono::steady_clock::now();
    IRIS_LOG_INFO(L"FileProvider 初始化开始");

    {
        std::lock_guard lock(deltaMutex_);
        if (!delta_) delta_ = std::make_unique<UsnDelta>();
    }

    // ── 快速路径：既有快照 → 加载映射视图（秒级可搜、专用内存近零增量）。
    //    数据新鲜度由随后的全量重建校准（重建后重写快照并切换视图）。──
    if (!snapshotPath_.empty()) {
        if (auto mapped = IndexSnapshot::LoadMapped(snapshotPath_)) {
            const size_t n = mapped->size();
            ReplaceIndex(std::move(mapped));
            totalFiles_.store(n, std::memory_order_relaxed);
            ready_.store(true, std::memory_order_release);
            IRIS_LOG_INFO(L"FileProvider 快照加载就绪: 文件数=" + std::to_wstring(n) +
                          L"（后台重建校准中）");
            emit dataChanged();
        } else {
            IRIS_LOG_INFO(L"FileProvider 无有效快照，走全量重建");
        }
    }

    auto store = BuildFullIndex();
    if (!store) {
        IRIS_LOG_INFO(L"FileProvider 初始化已取消");
        return false;
    }
    FinalizeAndSwap(std::move(store));

    // ── 启动 USN 监听（轮询增量 → delta → 秒级可见；阈值触发 compact）──
    if (!watchThread_.joinable())
        watchThread_ = std::thread([this]() { UsnWatchLoop(); });

    IRIS_LOG_INFO(L"FileProvider 初始化总耗时=" +
                  std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - initStart)
                                      .count()) + L"ms");
    return true;
}

std::shared_ptr<IndexStore> FileProvider::BuildFullIndex() {
    // compact 重建会再次进入：先关旧卷句柄、清卷表（避免句柄泄漏与重复轮询）
    {
        std::lock_guard lock(volumesMutex_);
        for (auto& v : volumes_) {
            if (v.hVolume != INVALID_HANDLE_VALUE) {
                CloseHandle(v.hVolume);
                v.hVolume = INVALID_HANDLE_VALUE;
            }
        }
        volumes_.clear();
    }

    std::vector<std::wstring> drives = WinUtil::GetLocalDrives();
    auto store = std::make_shared<IndexStore>();
    std::unordered_map<uint64_t, uint16_t> dirIdByFrn;

    for (const auto& root : drives) {
        if (cancelInitialize_.load(std::memory_order_acquire)) return nullptr;
        VolumeInfo vol;
        vol.rootPath = root;
        vol.driveLetter = root.empty() ? 0 : root[0];
        vol.isNTFS = WinUtil::IsNTFSVolume(root);

        if (vol.isNTFS) {
            if (!BuildVolumeIndex(vol, *store, dirIdByFrn)) {
                IRIS_LOG_WARN(L"NTFS 索引失败，降级遍历: " + root);
                FallbackScan(vol, *store, dirIdByFrn);
            }
        } else {
            IRIS_LOG_INFO(L"非 NTFS 卷，降级遍历: " + root);
            FallbackScan(vol, *store, dirIdByFrn);
        }
        // 保存卷信息用于增量（BuildVolumeIndex 已写回句柄与 USN 游标）
        std::lock_guard lock(volumesMutex_);
        volumes_.push_back(std::move(vol));
    }

    if (cancelInitialize_.load(std::memory_order_acquire)) return nullptr;
    store->Sort();
    store->BuildTrigramIndex();  // 构建 trigram 倒排索引（子串搜索用）
    return store;
}

void FileProvider::FinalizeAndSwap(std::shared_ptr<IndexStore> store) {
    const size_t files = store->size();

    bool swappedMapped = false;
    if (!snapshotPath_.empty()) {
        // 先切堆 store 顶住搜索（无缝），同时解除旧快照的映射锁——
        // 被本进程映射中的文件不可被 rename 替换，否则落盘的原子替换必失败。
        ReplaceIndex(std::const_pointer_cast<const IndexStore>(store));
        if (IndexSnapshot::Save(*store, snapshotPath_)) {
            if (auto mapped = IndexSnapshot::LoadMapped(snapshotPath_)) {
                ReplaceIndex(std::move(mapped));
                swappedMapped = true;  // 堆 store 随 shared_ptr 出栈释放
            } else {
                IRIS_LOG_WARN(L"快照重映射失败，保持堆后端");
            }
        } else {
            IRIS_LOG_WARN(L"快照落盘失败，保持堆后端");
        }
    } else {
        ReplaceIndex(std::const_pointer_cast<const IndexStore>(store));
    }
    totalFiles_.store(files, std::memory_order_relaxed);
    ready_.store(true, std::memory_order_release);

    {   // compact 重建后 delta 清零（其变更已并入新索引）
        std::lock_guard lock(deltaMutex_);
        if (delta_) delta_->Clear();
    }

    IRIS_LOG_INFO(L"FileProvider 就绪: 文件数=" + std::to_wstring(files) +
                  L" 后端=" + (swappedMapped ? L"mmap" : L"heap") +
                  L" 索引内存=" +
                  std::to_wstring(store->TotalMemoryBytes() / (1024 * 1024)) + L"MB");
    emit dataChanged();
}

// ============================================================================
// BuildVolumeIndex：单个 NTFS 卷的全量索引
// ============================================================================
bool FileProvider::BuildVolumeIndex(VolumeInfo& vol, IndexStore& store,
                                    std::unordered_map<uint64_t, uint16_t>& dirIdByFrn) {
    const std::wstring volHandlePath = L"\\\\.\\" + std::wstring(1, vol.driveLetter) + L":";
    HANDLE h = CreateFileW(volHandlePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        IRIS_LOG_WARN(L"打开卷句柄失败 err=" + std::to_wstring(GetLastError()) +
                      L" : " + volHandlePath);
        return false;
    }

    // 查询 USN Journal 状态
    USN_JOURNAL_DATA jd{};
    DWORD br = 0;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &jd, sizeof(jd), &br, nullptr)) {
        IRIS_LOG_WARN(L"FSCTL_QUERY_USN_JOURNAL 失败 err=" + std::to_wstring(GetLastError()) +
                      L" : " + volHandlePath);
        CloseHandle(h);
        return false;
    }

    // 全量枚举：FSCTL_ENUM_USN_DATA V0，MinFRN=0, MaxFRN=max
    // 先收集所有 TempEntry（FRN → name/parent/attrs）
    std::unordered_map<uint64_t, TempEntry> all;
    all.reserve(1 << 16);

    MFT_ENUM_DATA_V0 med{};
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = jd.NextUsn;

    constexpr DWORD kBufSize = 64 * 1024;
    std::vector<BYTE> buffer(kBufSize);

    while (true) {
        if (cancelInitialize_.load(std::memory_order_acquire)) {
            CloseHandle(h);
            return false;  // 取消：关闭卷句柄，不泄漏
        }
        br = 0;
        BOOL ok = DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &med, sizeof(med),
                                  buffer.data(), kBufSize, &br, nullptr);
        if (!ok || br < sizeof(USN)) break;

        // 缓冲区前 8 字节 = 下一次枚举的起始 FRN
        USN nextUsn = *reinterpret_cast<USN*>(buffer.data());
        DWORD off = sizeof(USN);
        while (off + sizeof(USN_RECORD) <= br) {
            const USN_RECORD* rec = reinterpret_cast<const USN_RECORD*>(buffer.data() + off);
            TempEntry te;
            DWORD len = ParseUsnRecord(rec, te);
            if (len == 0) break;
            all[te.frn] = std::move(te);
            off += len;
        }
        // 继续：更新 StartFRN 为返回的下一个 USN（V0 用 NextUsn 继续）
        med.StartFileReferenceNumber = nextUsn;
        if (br < kBufSize) break;  // 读完
    }

    // 第一遍：建目录表（所有 FILE_ATTRIBUTE_DIRECTORY 条目）
    StringPool& namePool = store.NamePool();
    StringPool& dirPool  = store.DirPool();
    std::unordered_map<uint64_t, std::wstring> dirPathCache;
    uint8_t depth = 0;
    for (auto& [frn, te] : all) {
        if (!(te.attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring path = BuildDirPath(frn, all, vol.rootPath, dirPathCache, depth);
        DirEntry de{};
        de.pathOffset = dirPool.Insert(path);
        de.pathLength = static_cast<uint16_t>(path.size());
        de.parentDirId = UINT32_MAX;
        uint16_t id = store.AddDir(de);
        dirIdByFrn[frn] = id;
    }

    // 第二遍：建文件条目
    for (auto& [frn, te] : all) {
        if (te.attributes & FILE_ATTRIBUTE_DIRECTORY) continue;  // 目录跳过
        if (ShouldExclude(te.attributes, te.name, excludeHidden_, excludeSystem_)) continue;

        auto dit = dirIdByFrn.find(te.parentFrn);
        uint16_t dirId = (dit != dirIdByFrn.end()) ? dit->second : 0;

        // 计算路径深度
        uint8_t pdepth = 0;
        BuildDirPath(te.parentFrn, all, vol.rootPath, dirPathCache, pdepth);

        CompactFileEntry e{};
        e.nameOffset = namePool.Insert(te.name);
        e.nameLength = static_cast<uint16_t>(te.name.size());
        e.type       = static_cast<uint32_t>(ItemType::FILE);
        e.dirId      = dirId;
        e.pathDepth  = pdepth;
        e.flags      = static_cast<uint8_t>(
            ((te.attributes & FILE_ATTRIBUTE_HIDDEN) ? 0x01 : 0) |
            ((te.attributes & FILE_ATTRIBUTE_SYSTEM) ? 0x02 : 0));
        e.usnLow     = static_cast<uint32_t>(frn & 0xFFFFFFFF);  // 占位（FRN 低 32 位）
        store.Insert(e);
    }

    // 增量游标与监听句柄写回调用方卷对象（h 所有权随 vol 移交 volumes_，Shutdown 统一关闭）
    vol.hVolume = h;
    vol.lastUsn = jd.NextUsn;
    vol.journalId = jd.UsnJournalID;
    return true;
}

// ============================================================================
// FallbackScan：非 NTFS 卷的递归遍历降级
// ============================================================================
void FileProvider::FallbackScan(const VolumeInfo& vol, IndexStore& store,
                                std::unordered_map<uint64_t, uint16_t>& /*dirIdByFrn*/) {
    StringPool& namePool = store.NamePool();
    StringPool& dirPool  = store.DirPool();
    namespace fs = std::filesystem;
    std::error_code ec;

    // 用目录路径字符串做去重的 dirId
    std::unordered_map<std::wstring, uint16_t> dirIdByPath;

    // 显式遍历：reparse point（junction/symlink）目录 disable_recursion_pending 不进入，
    // 杜绝 AppData\Local\Application Data 这类循环 junction（指向自身）导致
    // recursive_directory_iterator 无限递归、路径无限增长直至崩溃 0xc0000409。
    // 注意：std::filesystem 的 symlink_status().type() 对 junction 不一定返回 reparse_point，
    // 故改用 GetFileAttributesW 的 FILE_ATTRIBUTE_REPARSE_POINT 位（Windows 原生，可靠）。
    uint64_t files = 0, reparseSkipped = 0;
    auto it = fs::recursive_directory_iterator(vol.rootPath,
            fs::directory_options::skip_permission_denied, ec);
    for (const auto end = fs::recursive_directory_iterator(); it != end; it.increment(ec)) {
        if (cancelInitialize_.load(std::memory_order_acquire)) return;
        if (ec) { ec.clear(); continue; }
        const auto& path = it->path();

        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;  // 无权访问等，跳过

        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            ++reparseSkipped;
            it.disable_recursion_pending();  // 不进入 junction/symlink 子目录
            continue;
        }
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) continue;  // 目录不入文件索引

        const std::wstring fname = path.filename().wstring();
        if (fname.empty()) continue;
        if (ShouldExclude(attrs, fname, excludeHidden_, excludeSystem_)) continue;

        std::wstring dir = path.parent_path().wstring();
        uint16_t dirId = 0;
        auto dit = dirIdByPath.find(dir);
        if (dit == dirIdByPath.end()) {
            DirEntry de{};
            de.pathOffset = dirPool.Insert(dir);
            de.pathLength = static_cast<uint16_t>(dir.size());
            de.parentDirId = UINT32_MAX;
            dirId = store.AddDir(de);
            dirIdByPath[dir] = dirId;
        } else {
            dirId = dit->second;
        }

        CompactFileEntry e{};
        e.nameOffset = namePool.Insert(fname);
        e.nameLength = static_cast<uint16_t>(fname.size());
        e.type       = static_cast<uint32_t>(ItemType::FILE);
        e.dirId      = dirId;
        e.pathDepth  = static_cast<uint8_t>(StringUtil::GetPathDepth(dir));
        e.flags      = 0;
        e.usnLow     = 0;
        store.Insert(e);
        ++files;
    }
    IRIS_LOG_INFO(L"FallbackScan 完成: " + vol.rootPath +
                  L" 文件=" + std::to_wstring(files) +
                  L" 跳过reparse=" + std::to_wstring(reparseSkipped));
}

// ============================================================================
// Refresh：增量 USN 更新（失败则全量重建）
// ============================================================================
void FileProvider::Refresh() {
    // 简化实现：增量失败则触发完整重建（保持索引新鲜）。
    // 完整的增量（FSCTL_READ_USN_JOURNAL + CREATE/DELETE/RENAME 处理）在
    // 大规模部署时再精细化。此处优先保证正确性。
    bool changed = false;
    // 尝试增量；任何异常都退化到全量。
    try {
        // TODO(P1 精细化): 对每个卷读 USN 增量并应用。
        // 此处暂时只重新全量构建（30s 间隔下可接受，或后续优化）。
        (void)changed;
    } catch (...) {
        IRIS_LOG_WARN(L"FileProvider 增量异常");
    }
    // 当前实现：不做增量；由 App 的定时全量重建负责刷新（见 IndexCoordinator）。
}

// ============================================================================
// CoW
// ============================================================================
std::shared_ptr<const IndexStore> FileProvider::Snapshot() const {
    std::lock_guard lock(indexSwapMutex_);
    return index_;
}

void FileProvider::ReplaceIndex(std::shared_ptr<const IndexStore> next) {
    std::lock_guard lock(indexSwapMutex_);
    index_ = std::move(next);
    // 随 index_ CoW 重建组合视图（base 代际与 delta 快照绑定，搜索读侧一致）
    std::shared_ptr<UsnDelta> deltaSnap;
    {
        std::lock_guard d(deltaMutex_);
        if (delta_) deltaSnap = std::make_shared<UsnDelta>(*delta_);
    }
    overlay_ = deltaSnap
        ? std::make_shared<const DeltaOverlay>(index_, *deltaSnap)
        : std::make_shared<const DeltaOverlay>(index_, UsnDelta{});
}

std::shared_ptr<const DeltaOverlay> FileProvider::Overlay() const {
    std::lock_guard lock(indexSwapMutex_);
    return overlay_;
}

// ============================================================================
// USN 实时增量（监听线程）
// ============================================================================

namespace {

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

void FileProvider::UsnWatchLoop() {
    {
        std::lock_guard vl(volumesMutex_);
        IRIS_LOG_INFO(L"USN 监听线程启动（轮询 2s，NTFS 卷数=" +
                      std::to_wstring(static_cast<unsigned long>(
                          std::count_if(volumes_.begin(), volumes_.end(),
                                        [](const VolumeInfo& v) { return v.isNTFS; }))) +
                      L"，句柄有效=" +
                      std::to_wstring(static_cast<unsigned long>(
                          std::count_if(volumes_.begin(), volumes_.end(),
                                        [](const VolumeInfo& v) {
                                            return v.hVolume != INVALID_HANDLE_VALUE;
                                        }))) +
                      L"）");
    }
    while (!stopWatch_.load(std::memory_order_acquire)) {
        bool journalInvalid = false;
        {
            // 锁序恒为 volumesMutex_ → deltaMutex_（与 FinalizeAndSwap 无逆序）
            std::lock_guard vl(volumesMutex_);
            std::lock_guard lock(deltaMutex_);
            for (auto& v : volumes_) {
                if (!v.isNTFS) continue;
                if (!ReadVolumeUsnDelta(v)) { journalInvalid = true; break; }
            }
        }

        if (journalInvalid) {
            IRIS_LOG_WARN(L"USN journal 失效，触发全量重建兜底");
            TriggerRebuild();
        } else {
            bool needCompact = false;
            {
                std::lock_guard lock(deltaMutex_);
                needCompact = delta_ && delta_->ShouldCompact(NowMs());
            }
            if (needCompact) {
                IRIS_LOG_INFO(L"UsnDelta 达到阈值，触发 compact 重建");
                TriggerRebuild();
            }
        }

        if (rebuilding_.load(std::memory_order_acquire)) {
            // 重建期间暂停轮询（卷句柄将重建），1s 间隔探测结束
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    IRIS_LOG_INFO(L"USN 监听线程退出");
}

// 调用方须持有 deltaMutex_（delta_->Apply 在锁内执行）
bool FileProvider::ReadVolumeUsnDelta(VolumeInfo& vol) {
    READ_USN_JOURNAL_DATA_V0 rud{};
    rud.StartUsn = vol.lastUsn;
    rud.ReasonMask = 0xFFFFFFFF;  // 关注全部 reason（delta 内部按位分派）
    rud.ReturnOnlyOnClose = FALSE;
    rud.Timeout = 0;
    rud.BytesToWaitFor = 0;       // 非阻塞：立即返回当前增量
    rud.UsnJournalID = vol.journalId;

    constexpr DWORD kBufSize = 64 * 1024;
    std::vector<BYTE> buf(kBufSize);
    DWORD br = 0;
    if (!DeviceIoControl(vol.hVolume, FSCTL_READ_USN_JOURNAL, &rud, sizeof(rud),
                         buf.data(), kBufSize, &br, nullptr)) {
        const DWORD err = GetLastError();
        if (err == ERROR_HANDLE_EOF) return true;  // 无增量
        IRIS_LOG_WARN(L"FSCTL_READ_USN_JOURNAL 失败 err=" + std::to_wstring(err) +
                      L" 卷=" + vol.rootPath);
        return false;  // journal id 失配/句柄失效等 → 兜底重建
    }

    const USN nextUsn = *reinterpret_cast<const USN*>(buf.data());
    DWORD off = sizeof(USN);
    UsnEvent ev;
    while (off + sizeof(USN_RECORD_V2) <= br) {
        const size_t used = ParseUsnRecord(buf.data() + off, br - off, ev);
        if (used == 0) break;
        off += used;
        if (ev.attributes & FILE_ATTRIBUTE_DIRECTORY) continue;  // 目录事件忽略
        if (ShouldExclude(ev.attributes, ev.name, excludeHidden_, excludeSystem_)) continue;
        delta_->Apply(ev, NowMs());
    }
    vol.lastUsn = nextUsn;
    return true;
}

void FileProvider::TriggerRebuild() {
    bool expected = false;
    if (!rebuilding_.compare_exchange_strong(expected, true)) return;  // 重建中
    // 上一次重建线程已结束（rebuilding_==false 保证）；detach 结束线程避免 join 竞态
    if (rebuildThread_.joinable()) rebuildThread_.detach();
    rebuildThread_ = std::thread([this]() {
        auto store = BuildFullIndex();
        if (store) FinalizeAndSwap(std::move(store));
        rebuilding_.store(false, std::memory_order_release);
    });
}

// ============================================================================
// ISearchableProvider 查询
// ============================================================================
size_t FileProvider::FindFirstPrefix(std::wstring_view prefix) const {
    auto s = Snapshot();
    return s ? s->FindFirstPrefix(prefix) : 0;
}

ResultItem FileProvider::BuildResultItem(size_t index) const {
    auto ov = Overlay();
    if (!ov) return {};
    if (ov->IsDelta(index)) {
        const auto& c = ov->DeltaChange(index - ov->BaseSize());
        ResultItem item;
        item.title     = c.name;
        item.type      = ItemType::FILE;
        item.pathDepth = 1;
        // v1：delta 条目无父目录路径映射 → path 空（OpenItem 对空路径安全忽略），
        // compact 后由基础索引提供完整路径
        return item;
    }
    if (ov->Base() == nullptr || index >= ov->BaseSize()) return {};
    return ov->Base()->ToResultItem((*ov->Base())[index]);
}

std::wstring FileProvider::GetSearchText(size_t index) const {
    auto ov = Overlay();
    if (!ov) return {};
    if (ov->IsDelta(index)) return ov->DeltaChange(index - ov->BaseSize()).name;
    if (ov->Base() == nullptr || index >= ov->BaseSize()) return {};
    const auto& e = (*ov->Base())[index];
    std::wstring text;
    text.reserve(e.nameLength + 16);
    text += std::wstring(ov->Base()->NameAt(e));
    text += L' ';
    text += std::wstring(ov->Base()->DirOf(e));
    return text;
}

ItemType FileProvider::GetType(size_t index) const {
    auto ov = Overlay();
    if (!ov || ov->IsDelta(index)) return ItemType::FILE;
    if (ov->Base() == nullptr || index >= ov->BaseSize()) return ItemType::FILE;
    return static_cast<ItemType>((*ov->Base())[index].type);
}

uint8_t FileProvider::GetPathDepth(size_t index) const {
    auto ov = Overlay();
    if (!ov || ov->IsDelta(index)) return 1;
    if (ov->Base() == nullptr || index >= ov->BaseSize()) return 0;
    return (*ov->Base())[index].pathDepth;
}

std::wstring FileProvider::GetPath(size_t index) const {
    auto ov = Overlay();
    if (!ov || ov->IsDelta(index)) return {};  // delta 条目 v1 无完整路径
    if (ov->Base() == nullptr || index >= ov->BaseSize()) return {};
    const auto& e = (*ov->Base())[index];
    auto dir = ov->Base()->DirOf(e);
    auto name = ov->Base()->NameAt(e);
    std::wstring path;
    path.reserve(dir.size() + 1 + name.size());
    path.assign(dir);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path += L'\\';
    path.append(name);
    return path;
}

std::vector<uint32_t> FileProvider::SearchTrigram(std::wstring_view pattern) const {
    auto candidates = ISearchableProvider::SearchTrigram(pattern);  // base 查询
    auto ov = Overlay();
    if (!ov) return candidates;
    std::wstring lower;
    lower.reserve(pattern.size());
    for (wchar_t c : pattern)
        lower.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));

    // base 候选过滤已删除；追加 delta 新增/改名候选（delta 空间 index）
    std::vector<uint32_t> out;
    out.reserve(candidates.size() + 8);
    for (uint32_t idx : candidates)
        if (!ov->IsRemoved(idx)) out.push_back(idx);
    for (uint32_t rel : ov->DeltaSubstringCandidates(lower))
        out.push_back(static_cast<uint32_t>(ov->BaseSize() + rel));
    return out;
}

const TrigramIndex* FileProvider::GetTrigramIndex() const {
    auto s = Snapshot();
    return s ? &s->GetTrigramIndex() : nullptr;
}

// ============================================================================
// IProvider
// ============================================================================
size_t FileProvider::GetCount() const {
    auto s = Snapshot();
    return s ? s->size() : 0;
}

std::vector<ResultItem> FileProvider::GetAll() const {
    std::vector<ResultItem> out;
    auto s = Snapshot();
    if (!s) return out;
    out.reserve(s->size());
    for (size_t i = 0; i < s->size(); ++i)
        out.push_back(s->ToResultItem((*s)[i]));
    return out;
}

void FileProvider::Shutdown() {
    CancelInitialize();  // 令重建线程（若在跑）快速退出
    stopWatch_.store(true, std::memory_order_release);
    if (watchThread_.joinable()) watchThread_.join();
    if (rebuildThread_.joinable()) rebuildThread_.join();
    for (auto& v : volumes_) {
        if (v.hVolume != INVALID_HANDLE_VALUE) {
            CloseHandle(v.hVolume);
            v.hVolume = INVALID_HANDLE_VALUE;
        }
    }
    volumes_.clear();
    ready_.store(false, std::memory_order_release);
    ReplaceIndex(nullptr);
}

} // namespace iris
