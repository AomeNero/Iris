// Iris Core —— 热键字符串 ↔ (modifiers, vkCode) 解析（纯逻辑，无 Qt 依赖）
// 格式: Mod[+Mod]+Key，如 "Alt+Space"、"Ctrl+Alt+P"、"Win+Space"。
#pragma once

#include <string>

namespace iris {
namespace HotkeySpec {

/// 解析 spec → (modifiers 位掩码, vkCode)。失败返回 false（不修改输出参数）。
bool Parse(const std::string& spec, unsigned& modifiers, unsigned& vkCode);
/// 反向序列化，如 (MOD_ALT, VK_SPACE) → "Alt+Space"。
std::string ToString(unsigned modifiers, unsigned vkCode);

} // namespace HotkeySpec
} // namespace iris
