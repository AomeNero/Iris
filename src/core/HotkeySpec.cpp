// Iris Core —— 热键字符串解析实现
#include "core/HotkeySpec.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // MOD_* / VK_* 常量

namespace iris {
namespace HotkeySpec {

namespace {

std::string ToLowerStr(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string TrimStr(std::string s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool ModifierBit(const std::string& tok, unsigned& mods) {
    const std::string s = ToLowerStr(tok);
    if (s == "alt")                                                  { mods |= MOD_ALT;     return true; }
    if (s == "ctrl" || s == "control")                               { mods |= MOD_CONTROL; return true; }
    if (s == "shift")                                                { mods |= MOD_SHIFT;   return true; }
    if (s == "win" || s == "windows" || s == "super" || s == "meta") { mods |= MOD_WIN;     return true; }
    return false;
}

bool KeyVk(const std::string& tok, unsigned& vk) {
    const std::string s = ToLowerStr(tok);
    if (s.size() == 1) {
        const char c = s[0];
        if (c >= 'a' && c <= 'z') { vk = static_cast<unsigned>('A' + (c - 'a')); return true; }
        if (c >= '0' && c <= '9') { vk = static_cast<unsigned>('0' + (c - '0')); return true; }
        return false;
    }
    if (s.size() >= 2 && s[0] == 'f' &&
        std::all_of(s.begin() + 1, s.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        const int n = std::stoi(s.substr(1));
        if (n >= 1 && n <= 24) { vk = static_cast<unsigned>(VK_F1 + (n - 1)); return true; }
    }
    static const std::unordered_map<std::string, unsigned> named = {
        {"space", VK_SPACE}, {"enter", VK_RETURN}, {"return", VK_RETURN},
        {"tab", VK_TAB}, {"esc", VK_ESCAPE}, {"escape", VK_ESCAPE}, {"backspace", VK_BACK},
        {"insert", VK_INSERT}, {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END},
        {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT},
        {"left", VK_LEFT}, {"up", VK_UP}, {"right", VK_RIGHT}, {"down", VK_DOWN},
    };
    const auto it = named.find(s);
    if (it != named.end()) { vk = it->second; return true; }
    return false;
}

std::string KeyName(unsigned vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    switch (vk) {
        case VK_SPACE: return "Space";    case VK_RETURN: return "Enter";
        case VK_TAB:   return "Tab";      case VK_ESCAPE: return "Esc";
        case VK_BACK:  return "Backspace";
        case VK_INSERT: return "Insert";  case VK_DELETE: return "Delete";
        case VK_HOME:  return "Home";     case VK_END:    return "End";
        case VK_PRIOR: return "PageUp";   case VK_NEXT:   return "PageDown";
        case VK_LEFT:  return "Left";     case VK_UP:     return "Up";
        case VK_RIGHT: return "Right";    case VK_DOWN:   return "Down";
        default: return std::to_string(vk);
    }
}

} // namespace

bool Parse(const std::string& spec, unsigned& modifiers, unsigned& vkCode) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : spec) {
        if (c == '+') { tokens.push_back(TrimStr(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    tokens.push_back(TrimStr(cur));

    if (tokens.size() < 2) return false;  // 至少 1 修饰键 + 1 主键
    unsigned mods = 0;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].empty() || !ModifierBit(tokens[i], mods)) return false;
    }
    if (tokens.back().empty()) return false;
    unsigned vk = 0;
    if (!KeyVk(tokens.back(), vk)) return false;
    if (mods == 0) return false;

    modifiers = mods;
    vkCode = vk;
    return true;
}

std::string ToString(unsigned modifiers, unsigned vkCode) {
    std::string s;
    if (modifiers & MOD_WIN)     s += "Win+";
    if (modifiers & MOD_CONTROL) s += "Ctrl+";
    if (modifiers & MOD_ALT)     s += "Alt+";
    if (modifiers & MOD_SHIFT)   s += "Shift+";
    s += KeyName(vkCode);
    return s;
}

} // namespace HotkeySpec
} // namespace iris
