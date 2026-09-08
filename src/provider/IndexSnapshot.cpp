// Iris 数据模型层 —— IndexSnapshot 实现
#include "provider/IndexSnapshot.h"

#include "core/Logger.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace iris {

namespace {

constexpr char kMagic[4] = {'I', 'R', 'I', 'X'};
constexpr uint32_t kVersion = 1;

#pragma pack(push, 1)
struct SnapshotHeader {
    char     magic[4];
    uint32_t version;
    uint32_t entryCount;
    uint32_t dirCount;
    uint64_t namePoolChars;
    uint64_t dirPoolChars;
    uint32_t trigramCount;
    uint32_t postingCount;
};
#pragma pack(pop)
static_assert(sizeof(SnapshotHeader) == 40, "头部布局须稳定（格式兼容性）");

template <typename T>
bool WritePod(std::ofstream& ofs, const T* data, size_t count) {
    ofs.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(count * sizeof(T)));
    return ofs.good();
}

// 解析并校验头部（magic/版本/段总长==文件长）
struct ParsedHeader {
    SnapshotHeader h;
    bool           valid = false;
};

ParsedHeader ParseHeader(const char* data, size_t size) {
    ParsedHeader r;
    if (size < sizeof(SnapshotHeader)) return r;
    std::memcpy(&r.h, data, sizeof(r.h));
    if (std::memcmp(r.h.magic, kMagic, sizeof(r.h.magic)) != 0) return r;
    if (r.h.version != kVersion) return r;
    const uint64_t need =
        sizeof(SnapshotHeader) +
        uint64_t(r.h.entryCount) * sizeof(CompactFileEntry) +
        uint64_t(r.h.dirCount) * sizeof(DirEntry) +
        r.h.namePoolChars * sizeof(wchar_t) +
        r.h.dirPoolChars * sizeof(wchar_t) +
        uint64_t(r.h.trigramCount) * sizeof(uint32_t) +
        (uint64_t(r.h.trigramCount) + 1) * sizeof(uint32_t) +
        uint64_t(r.h.postingCount) * sizeof(uint32_t);
    if (need != size) return r;
    r.valid = true;
    return r;
}

} // namespace

bool IndexSnapshot::Save(const IndexStore& store,
                         const std::filesystem::path& path) {
    const TrigramCsrView csr = store.GetTrigramIndex().ExportCsrView();

    SnapshotHeader h{};
    std::memcpy(h.magic, kMagic, sizeof(h.magic));
    h.version       = kVersion;
    h.entryCount    = static_cast<uint32_t>(store.size());
    h.dirCount      = static_cast<uint32_t>(store.DirCount());
    h.namePoolChars = store.namePool_.SizeInBytes() / sizeof(wchar_t);
    h.dirPoolChars  = store.dirPool_.SizeInBytes() / sizeof(wchar_t);
    h.trigramCount  = static_cast<uint32_t>(csr.trigramCount);
    h.postingCount  = static_cast<uint32_t>(csr.postingCount);

    // 原子替换：先写 <path>.tmp，全部成功后 rename 覆盖目标（失败不触碰旧快照）
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            IRIS_LOG_WARN(L"快照写入失败（无法创建临时文件）: " + tmp.wstring());
            return false;
        }
        bool ok = WritePod(ofs, &h, 1);
        if (ok && h.entryCount)
            ok = WritePod(ofs, store.entries_.data(), h.entryCount);
        if (ok && h.dirCount)
            ok = WritePod(ofs, store.dirs_.data(), h.dirCount);
        if (ok && h.namePoolChars)
            ok = WritePod(ofs, store.namePool_.Data(), h.namePoolChars);
        if (ok && h.dirPoolChars)
            ok = WritePod(ofs, store.dirPool_.Data(), h.dirPoolChars);
        if (ok && h.trigramCount)
            ok = WritePod(ofs, csr.keys, h.trigramCount);
        // offsets 恒有 trigramCount+1 项（空快照写一个 0）
        if (ok) {
            if (csr.trigramCount) {
                ok = WritePod(ofs, csr.offsets, csr.trigramCount + 1);
            } else {
                const uint32_t zero = 0;
                ok = WritePod(ofs, &zero, 1);
            }
        }
        if (ok && h.postingCount)
            ok = WritePod(ofs, csr.postings, h.postingCount);
        ofs.flush();
        ok = ok && ofs.good();
        if (!ok) {
            std::error_code ec;
            ofs.close();
            std::filesystem::remove(tmp, ec);  // 先关句柄再删，避免残留大文件
            IRIS_LOG_WARN(L"快照写入失败（写盘错误，检查磁盘空间）: " + path.wstring());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);  // MSVC 实现：覆盖已存在目标
    if (ec) {
        std::filesystem::remove(tmp, ec);
        // 常见原因：目标正被本进程映射视图锁定（须先解除映射再落盘）
        IRIS_LOG_WARN(L"快照原子替换失败 err=" + std::to_wstring(ec.value()) +
                      L" : " + path.wstring());
        return false;
    }
    return true;
}

std::unique_ptr<IndexStore> IndexSnapshot::Load(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < sizeof(SnapshotHeader)) return nullptr;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return nullptr;
    std::vector<char> buf(static_cast<size_t>(fileSize));
    if (!ifs.read(buf.data(), static_cast<std::streamsize>(fileSize))) return nullptr;

    const ParsedHeader ph = ParseHeader(buf.data(), buf.size());
    if (!ph.valid) return nullptr;
    const SnapshotHeader& h = ph.h;

    auto store = std::make_unique<IndexStore>();
    const char* p = buf.data() + sizeof(SnapshotHeader);
    auto take = [&p](size_t bytes) {
        const char* out = p;
        p += bytes;
        return out;
    };

    const auto* entries = reinterpret_cast<const CompactFileEntry*>(
        take(size_t(h.entryCount) * sizeof(CompactFileEntry)));
    store->entries_.assign(entries, entries + h.entryCount);

    const auto* dirs = reinterpret_cast<const DirEntry*>(
        take(size_t(h.dirCount) * sizeof(DirEntry)));
    store->dirs_.assign(dirs, dirs + h.dirCount);

    store->namePool_.AssignRaw(
        reinterpret_cast<const wchar_t*>(take(size_t(h.namePoolChars) * sizeof(wchar_t))),
        h.namePoolChars);
    store->dirPool_.AssignRaw(
        reinterpret_cast<const wchar_t*>(take(size_t(h.dirPoolChars) * sizeof(wchar_t))),
        h.dirPoolChars);

    const auto* keys = reinterpret_cast<const uint32_t*>(
        take(size_t(h.trigramCount) * sizeof(uint32_t)));
    const auto* offsets = reinterpret_cast<const uint32_t*>(
        take((size_t(h.trigramCount) + 1) * sizeof(uint32_t)));
    const auto* postings = reinterpret_cast<const uint32_t*>(
        take(size_t(h.postingCount) * sizeof(uint32_t)));

    store->trigramIndex_.ImportCsr(keys, offsets, postings, h.trigramCount, h.entryCount);
    return store;
}

std::unique_ptr<IndexStore> IndexSnapshot::LoadMapped(const std::filesystem::path& path) {
    MappedSnapshot mapped;
    if (!mapped.Open(path)) return nullptr;
    const ParsedHeader ph = ParseHeader(mapped.Data(), mapped.Size());
    if (!ph.valid) return nullptr;
    const SnapshotHeader& h = ph.h;

    auto store = std::make_unique<IndexStore>();
    store->mapped_ = std::move(mapped);  // 映射生命周期随 store
    const char* p = store->mapped_.Data() + sizeof(SnapshotHeader);
    auto advance = [&p](size_t bytes) {
        const char* out = p;
        p += bytes;
        return out;
    };

    store->view_.entries = reinterpret_cast<const CompactFileEntry*>(
        advance(size_t(h.entryCount) * sizeof(CompactFileEntry)));
    store->view_.entryCount = h.entryCount;
    store->view_.dirs = reinterpret_cast<const DirEntry*>(
        advance(size_t(h.dirCount) * sizeof(DirEntry)));
    store->view_.dirCount = h.dirCount;
    store->namePool_.AttachView(
        reinterpret_cast<const wchar_t*>(advance(size_t(h.namePoolChars) * sizeof(wchar_t))));
    store->dirPool_.AttachView(
        reinterpret_cast<const wchar_t*>(advance(size_t(h.dirPoolChars) * sizeof(wchar_t))));

    const auto* keys = reinterpret_cast<const uint32_t*>(
        advance(size_t(h.trigramCount) * sizeof(uint32_t)));
    const auto* offsets = reinterpret_cast<const uint32_t*>(
        advance((size_t(h.trigramCount) + 1) * sizeof(uint32_t)));
    const auto* postings = reinterpret_cast<const uint32_t*>(
        advance(size_t(h.postingCount) * sizeof(uint32_t)));

    store->trigramIndex_.AttachView(keys, offsets, postings, h.trigramCount, h.entryCount);
    return store;
}

} // namespace iris
