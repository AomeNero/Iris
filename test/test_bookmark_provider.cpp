// BookmarkProvider 单元测试：聚焦 JSON 解析与层级拼接（最易错的核心逻辑）
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "provider/BookmarkProvider.h"

using iris::BookmarkProvider;

namespace {

// 模拟 Chrome/Edge 的 Bookmarks 结构：多根节点 + 嵌套文件夹
const char* kSample = R"({
  "roots": {
    "bookmark_bar": {
      "name": "书签栏",
      "type": "folder",
      "children": [
        { "name": "Google", "url": "https://google.com", "type": "url" },
        { "name": "工作", "type": "folder", "children": [
            { "name": "GitHub", "url": "https://github.com", "type": "url" }
        ]}
      ]
    },
    "other": {
      "name": "其他",
      "children": [
        { "name": "Bing", "url": "https://bing.com", "type": "url" }
      ]
    }
  }
})";

} // namespace

TEST(BookmarkProviderTest, ParsesUrlsAndHierarchy) {
    std::vector<BookmarkProvider::Entry> out;
    BookmarkProvider::ParseBookmarksText(kSample, false, out);

    ASSERT_EQ(out.size(), 3u);
    // 遍历顺序：bookmark_bar 先于 other；文件夹内先 url 后子文件夹
    EXPECT_EQ(out[0].title,    L"Google");
    EXPECT_EQ(out[0].url,      L"https://google.com");
    EXPECT_EQ(out[0].subtitle, L"书签栏");

    EXPECT_EQ(out[1].title,    L"GitHub");
    EXPECT_EQ(out[1].url,      L"https://github.com");
    EXPECT_EQ(out[1].subtitle, L"书签栏 / 工作");  // 父级层级用 " / " 拼接

    EXPECT_EQ(out[2].title,    L"Bing");
    EXPECT_EQ(out[2].url,      L"https://bing.com");
    EXPECT_EQ(out[2].subtitle, L"其他");
}

TEST(BookmarkProviderTest, SkipsUrlNodesMissingFields) {
    const char* bad = R"({
      "roots": {
        "bookmark_bar": { "children": [
            { "name": "NoUrl", "type": "url" },
            { "url": "https://x.com", "type": "url" },
            { "name": "OK", "url": "https://ok.com", "type": "url" }
        ]}
      }
    })";
    std::vector<BookmarkProvider::Entry> out;
    BookmarkProvider::ParseBookmarksText(bad, false, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].title, L"OK");
}

TEST(BookmarkProviderTest, MalformedJsonYieldsNothing) {
    std::vector<BookmarkProvider::Entry> out;
    BookmarkProvider::ParseBookmarksText("not valid json", false, out);
    EXPECT_TRUE(out.empty());
}

TEST(BookmarkProviderTest, MissingRootsYieldsNothing) {
    std::vector<BookmarkProvider::Entry> out;
    BookmarkProvider::ParseBookmarksText(R"({ "version": 1 })", false, out);
    EXPECT_TRUE(out.empty());
}
