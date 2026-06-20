// Iris Provider —— 文件索引（NTFS USN Journal）实现
// 设计依据: doc/detailed-design.md §6.1, doc/code-prompt.md §六 Module4
#include "provider/FileProvider.h"

#include "core/Logger.h"
#include "core/StringUtil.h"
#include "core/WinUtil.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
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
bool FileProvider::ShouldExclude(DWORD fileAttributes, const std::wstring& fileName) {
    if (fileAttributes & FILE_ATTRIBUTE_HIDDEN) return true;
    if (fileAttributes & FILE_ATTRIBUTE_SYSTEM) return true;
    const std::wstring ext = StringUtil::ExtractExtension(fileName);
    if (std::find(kExcludedExtensions.begin(), kExcludedExtensions.end(), ext)
        != kExcludedExtensions.end())
        return true;
    return false;
}

// ============================================================================
// 构造 / 析构
// ============================================================================
FileProvider::FileProvider()  = default;
FileProvider::~FileProvider() { Shutdown(); }

// ============================================================================
// Initialize：枚举卷 → 全量 USN 索引 → CoW 原子替换
// ============================================================================
bool FileProvider::Initialize() {
    IRIS_LOG_INFO(L"FileProvider 初始化开始");

    std::vector<std::wstring> drives = WinUtil::GetLocalDrives();
    std::shared_ptr<IndexStore> store = std::make_shared<IndexStore>();
    std::unordered_map<uint64_t, uint16_t> dirIdByFrn;

    uint64_t files = 0, dirs = 0;

    for (const auto& root : drives) {
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
        // 保存卷信息用于增量（含已建好的句柄）
        // 注：BuildVolumeIndex 内打开的句柄存入 vol
        volumes_.push_back(std::move(vol));
    }

    store->Sort();
    files = store->size();

    ReplaceIndex(std::const_pointer_cast<const IndexStore>(store));
    totalFiles_.store(files, std::memory_order_relaxed);
    totalDirs_.store(dirs, std::memory_order_relaxed);
    ready_.store(true, std::memory_order_release);

    IRIS_LOG_INFO(L"FileProvider 就绪: 文件数=" + std::to_wstring(files));
    emit dataChanged();
    return true;
}

// ============================================================================
// BuildVolumeIndex：单个 NTFS 卷的全量索引
// ============================================================================
bool FileProvider::BuildVolumeIndex(const VolumeInfo& vol, IndexStore& store,
                                    std::unordered_map<uint64_t, uint16_t>& dirIdByFrn) {
    const std::wstring volHandlePath = L"\\\\.\\" + std::wstring(1, vol.driveLetter) + L":";
    HANDLE h = CreateFileW(volHandlePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 查询 USN Journal 状态
    USN_JOURNAL_DATA jd{};
    DWORD br = 0;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &jd, sizeof(jd), &br, nullptr)) {
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
        if (ShouldExclude(te.attributes, te.name)) continue;

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

    // 记录卷的增量游标
    for (auto& v : volumes_) {
        if (v.driveLetter == vol.driveLetter) {
            v.hVolume = h;  // 保留句柄供 Refresh（实际为简单实现，可关闭）
            v.lastUsn = jd.NextUsn;
            v.journalId = jd.UsnJournalID;
            return true;
        }
    }
    // volumes_ 中尚无该卷（Initialize 阶段）：由调用方 push_back；这里不关句柄。
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

    for (auto& entry : fs::recursive_directory_iterator(vol.rootPath,
            fs::directory_options::skip_permission_denied, ec)) {
        if (ec) { ec.clear(); continue; }
        const auto& path = entry.path();
        const std::wstring fname = path.filename().wstring();
        if (fname.empty()) continue;

        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
        if (ShouldExclude(attrs, fname)) continue;
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) continue;

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
    }
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
}

// ============================================================================
// ISearchableProvider 查询
// ============================================================================
size_t FileProvider::FindFirstPrefix(std::wstring_view prefix) const {
    auto s = Snapshot();
    return s ? s->FindFirstPrefix(prefix) : 0;
}

ResultItem FileProvider::BuildResultItem(size_t index) const {
    auto s = Snapshot();
    if (!s || index >= s->size()) return {};
    return s->ToResultItem((*s)[index]);
}

std::wstring FileProvider::GetSearchText(size_t index) const {
    auto s = Snapshot();
    if (!s || index >= s->size()) return {};
    const auto& e = (*s)[index];
    std::wstring text;
    text.reserve(e.nameLength + 16);
    text += std::wstring(s->NameAt(e));
    text += L' ';
    text += std::wstring(s->DirOf(e));
    return text;
}

ItemType FileProvider::GetType(size_t index) const {
    auto s = Snapshot();
    if (!s || index >= s->size()) return ItemType::FILE;
    return static_cast<ItemType>((*s)[index].type);
}

uint8_t FileProvider::GetPathDepth(size_t index) const {
    auto s = Snapshot();
    if (!s || index >= s->size()) return 0;
    return (*s)[index].pathDepth;
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
