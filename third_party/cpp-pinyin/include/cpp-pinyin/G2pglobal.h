#ifndef G2PGLOBAL_H
#define G2PGLOBAL_H

#include <filesystem>
#include <string>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::filesystem::path CPP_PINYIN_EXPORT dictionaryPath();

    void CPP_PINYIN_EXPORT setDictionaryPath(const std::filesystem::path &dir);

    // 内存 dict 注入（key 如 "mandarin/word.txt"）；命中则 DictUtil 从内存加载，免落盘
    void CPP_PINYIN_EXPORT setDictData(const std::string &name, const std::string &data);
    const std::string *CPP_PINYIN_EXPORT getDictData(const std::string &name);

    bool CPP_PINYIN_EXPORT isLetter(const char16_t &c);

    bool CPP_PINYIN_EXPORT isHanzi(const char16_t &c);

    bool CPP_PINYIN_EXPORT isKana(const char16_t &c);

    bool CPP_PINYIN_EXPORT isDigit(const char16_t &c);

    bool CPP_PINYIN_EXPORT isSpace(const char16_t &c);

    bool CPP_PINYIN_EXPORT isSpecialKana(const char16_t &c);
}

#endif // G2PGLOBAL_H
