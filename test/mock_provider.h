// 测试辅助：内存版 ISearchableProvider（按 title 排序，支持前缀扫描）
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>

#include "provider/ISearchableProvider.h"

class MockProvider : public iris::ISearchableProvider {
public:
    struct Entry {
        std::wstring  title;
        std::wstring  subtitle;
        std::wstring  path;
        iris::ItemType type = iris::ItemType::FILE;
        uint8_t       depth = 0;
    };
    using Entries = std::vector<Entry>;

    explicit MockProvider(std::vector<Entry> entries, bool readyFlag = true)
        : entries_(std::move(entries)), ready_(readyFlag) {
        // 按 title 字典序排序（使 FindFirstPrefix 与 Matcher 前缀扫描正确）
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) {
                      return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
                  });
    }

    bool Initialize() override { return true; }
    void Shutdown() override {}
    std::vector<iris::ResultItem> GetAll() const override {
        std::vector<iris::ResultItem> out;
        for (size_t i = 0; i < entries_.size(); ++i) out.push_back(BuildResultItem(i));
        return out;
    }
    size_t GetCount() const override { return entries_.size(); }
    std::wstring GetName() const override { return L"Mock"; }
    bool IsReady() const override { return ready_; }
    void Refresh() override {}

    size_t FindFirstPrefix(std::wstring_view prefix) const override {
        if (prefix.empty()) return 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (_wcsicmp(entries_[i].title.c_str(),
                         std::wstring(prefix).c_str()) >= 0)
                return i;
        }
        return entries_.size();
    }
    iris::ResultItem BuildResultItem(size_t i) const override {
        const Entry& e = entries_[i];
        iris::ResultItem r;
        r.title = e.title;
        r.subtitle = e.subtitle;
        r.path = e.path;
        r.type = e.type;
        r.pathDepth = e.depth;
        return r;
    }
    std::wstring GetSearchText(size_t i) const override {
        return entries_[i].title + L" " + entries_[i].path;
    }
    iris::ItemType GetType(size_t i) const override { return entries_[i].type; }
    uint8_t GetPathDepth(size_t i) const override { return entries_[i].depth; }

private:
    std::vector<Entry> entries_;
    bool ready_;
};
