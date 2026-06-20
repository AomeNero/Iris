// Qt 5.14.2 ↔ MSVC 14.5x (VS2026) 兼容 shim
//
// 背景: Qt 5.14.2 的 qcompilerdetection.h 把 QT_MAKE_CHECKED_ARRAY_ITERATOR
// 定义为 stdext::make_checked_array_iterator(...)，而 MSVC 14.5x 的 STL 已
// 彻底移除 stdext::checked_array_iterator（STL4043）。Qt 所有调用点传入的都是
// 裸指针，故此处把 stdext::make_*_array_iterator 实现为"直接返回裸指针"，
// 与 Qt 自带的 #ifndef fallback `(x)` 完全等价。通过 CMake 的 /FI 强制前置包含。
#pragma once

#include <cstddef>

namespace stdext {

template <typename T>
inline T* make_checked_array_iterator(T* p, std::size_t /*size*/) { return p; }

template <typename T>
inline T* make_unchecked_array_iterator(T* p) { return p; }

} // namespace stdext
