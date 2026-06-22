#include <cpp-pinyin/G2pglobal.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Pinyin
{
    class G2pGlobal {
    public:
        std::filesystem::path path;
    };

    auto m_global = std::make_unique<G2pGlobal>();

    std::filesystem::path dictionaryPath() {
        return m_global->path;
    }

    void setDictionaryPath(const std::filesystem::path &dir) {
        m_global->path = dir;
    }

    // 内存 dict 数据表（函数内 static，规避全局对象初始化顺序问题）
    std::unordered_map<std::string, std::string> &dictDataMap() {
        static std::unordered_map<std::string, std::string> g_dictData;
        return g_dictData;
    }

    void setDictData(const std::string &name, const std::string &data) {
        dictDataMap()[name] = data;
    }

    const std::string *getDictData(const std::string &name) {
        auto &m = dictDataMap();
        auto it = m.find(name);
        return it == m.end() ? nullptr : &it->second;
    }

    bool isLetter(const char16_t &c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    bool isHanzi(const char16_t &c) {
        return c >= 0x4e00 && c <= 0x9fa5;
    }

    bool isKana(const char16_t &c) {
        return (c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF);
    }

    bool isDigit(const char16_t &c) {
        return c >= '0' && c <= '9';
    }

    bool isSpace(const char16_t &c) {
        return c == ' ';
    }

    bool isSpecialKana(const char16_t &c) {
        static const std::unordered_set<char16_t> specialKana = {
            u'ャ', u'ュ', u'ョ', u'ゃ', u'ゅ', u'ょ',
            u'ァ', u'ィ', u'ゥ', u'ェ', u'ォ', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ'
        };
        return specialKana.find(c) != specialKana.end();
    }
}
