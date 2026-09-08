// Iris 数据模型层 —— 只读文件映射 RAII
// 承载索引快照的映射视图：映射页由页缓存承载（shareable），不计入进程
// 专用内存。仅读访问（PAGE_READONLY / FILE_MAP_READ），移动语义唯一所有权。
#pragma once

#include <cstddef>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>

namespace iris {

class MappedSnapshot {
public:
    MappedSnapshot() = default;
    ~MappedSnapshot() { Close(); }

    MappedSnapshot(const MappedSnapshot&)            = delete;
    MappedSnapshot& operator=(const MappedSnapshot&) = delete;

    MappedSnapshot(MappedSnapshot&& o) noexcept
        : hFile_(o.hFile_), hMap_(o.hMap_), data_(o.data_), size_(o.size_) {
        o.hFile_ = INVALID_HANDLE_VALUE;
        o.hMap_ = nullptr;
        o.data_ = nullptr;
        o.size_ = 0;
    }
    MappedSnapshot& operator=(MappedSnapshot&& o) noexcept {
        if (this != &o) {
            Close();
            hFile_ = o.hFile_;
            hMap_ = o.hMap_;
            data_ = o.data_;
            size_ = o.size_;
            o.hFile_ = INVALID_HANDLE_VALUE;
            o.hMap_ = nullptr;
            o.data_ = nullptr;
            o.size_ = 0;
        }
        return *this;
    }

    // 打开并整文件映射；已打开的映射会被先关闭。失败返回 false（内部已清理）。
    bool Open(const std::filesystem::path& path) {
        Close();
        hFile_ = CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(hFile_, &sz) || sz.QuadPart <= 0) { Close(); return false; }
        hMap_ = CreateFileMappingW(hFile_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap_) { Close(); return false; }
        data_ = static_cast<const char*>(MapViewOfFile(hMap_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) { Close(); return false; }
        size_ = static_cast<size_t>(sz.QuadPart);
        return true;
    }

    void Close() noexcept {
        if (data_) { UnmapViewOfFile(data_); data_ = nullptr; }
        if (hMap_) { CloseHandle(hMap_); hMap_ = nullptr; }
        if (hFile_ != INVALID_HANDLE_VALUE) { CloseHandle(hFile_); hFile_ = INVALID_HANDLE_VALUE; }
        size_ = 0;
    }

    const char* Data() const { return data_; }
    size_t      Size() const { return size_; }

private:
    HANDLE      hFile_ = INVALID_HANDLE_VALUE;
    HANDLE      hMap_  = nullptr;
    const char* data_  = nullptr;
    size_t      size_  = 0;
};

} // namespace iris
