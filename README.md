# 🌸 Iris

> Windows 极速桌面启动器——快捷键秒搜文件、应用与书签。

![CI](https://img.shields.io/github/actions/workflow/status/AomeNero/Iris/ci.yml?branch=main&label=CI&logo=github)
![Tests](https://img.shields.io/badge/tests-45%20passed-brightgreen?logo=googletest)
![Coverage](https://img.shields.io/badge/coverage-≥80%25-green)
![Version](https://img.shields.io/github/v/release/AomeNero/Iris?label=release&logo=iris)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-0078D4?logo=windows)
![License](https://img.shields.io/badge/license-MIT-blue)

---

## 1. 项目介绍

### 痛点与场景

Windows 自带的搜索慢且入口分散——找文件要去资源管理器、开软件要翻开始菜单、打开收藏的网址要先进浏览器。Everything 够快但只搜文件，Alfred 够优雅却只属于 macOS。**Iris 把"秒开任何东西"统一到一个快捷键里。**

典型场景：
- **秒开常用软件**：`Alt+Space` → 敲 `calc` → 回车，计算器立刻打开。
- **全盘找文件**：输入文件名片段，毫秒级列出全盘匹配，`Ctrl+1~9` 直接打开第 N 条。
- **直达浏览器书签**：搜 Chrome / Edge 收藏的网址，无需先打开浏览器翻收藏夹。

### 核心特性

- **快捷键秒唤**：全局热键（默认 `Alt+Space`）瞬间弹出搜索框，输入即搜、即开。
- **极速索引**：直接读取 NTFS USN Journal / MFT，全盘文件毫秒级检索，非 NTFS 自动回退递归扫描。
- **三源合一**：本地文件、开始菜单应用、Chrome/Edge 浏览器书签统一排序，按使用频次加权。
- **拼音搜索**：输入拼音直接命中中文（如 `weixin` 找「微信」），全拼 + 首字母，中英混搜无障碍。
- **键鼠并捷**：`Ctrl+1~9` 快捷打开对应行、回车/双击执行、上下键选择、`←/→/PageUp/Down` 分页翻页、鼠标侧键翻页、滚轮浏览、右键菜单（打开/打开路径/复制/属性）。
- **优雅便携**：QPainter 自绘 Alfred 风格界面，明/暗主题随心切换；单文件免安装、托盘驻留、低内存占用。

### 视觉演示

![Iris 搜索演示](/demo.gif)

*按 `Alt+Space` 唤出搜索框 → 输入关键词 → 实时列出结果 → 回车打开。鼠标悬停即选中，右侧 `ctrl1~9` 提示可用 `Ctrl+数字` 快捷打开。*

---

## 2. 快速上手

### 环境依赖

| 角色 | 要求 |
|------|------|
| **运行（普通用户）** | Windows 10 (1903+) / Windows 11，**无需任何依赖**——单文件便携 exe |
| **源码构建（开发者）** | CMake ≥ 3.16、MSVC (Visual Studio 2022 / 2026 工具集)、Qt 5.14.2 **静态库** (Core/Gui/Widgets/Svg) |

### 安装

**方式 A · 下载即用（推荐普通用户）**

前往 [Releases](https://github.com/AomeNero/Iris/releases) 下载 `Iris.exe`，放入任意目录双击运行，托盘出现 🌸 图标即就绪。

**方式 B · 源码构建（开发者）**

```powershell
git clone https://github.com/AomeNero/Iris.git
cd Iris
scripts\run-in-vcvars.cmd cmake -B build -Wno-dev   # 首次配置（需 MSVC 环境）
scripts\build.cmd                                     # 增量构建 Release
```

> 💡 本机无 `cl.exe` 在 PATH 时，所有需要 MSVC 的命令都通过 `scripts\` 下的 `.cmd` 包装器自动加载 vcvars。

### 最小可运行示例

```powershell
.\build\Release\Iris.exe
# 托盘出现 Iris 图标 → 按 Alt+Space 唤出搜索框 → 输入 "calc" → 回车打开计算器
```

预期：输入栏实时显示文字与闪烁光标，下方列出匹配结果，首行高亮为选中态，回车立即打开。

### 进阶配置

首次运行会在 **`Iris.exe` 同级目录**生成 `config.json`，修改后**重启 Iris** 生效。托盘右键「设置」可直接打开该文件。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `hotkey` | string | `"Alt+Space"` | 全局热键，格式 `Mod[+Mod]+Key`（如 `Ctrl+Alt+P`） |
| `theme` | string | `"light"` | 主题：`"light"` / `"dark"` |
| `maxResults` | int | `9` | 结果列表最大显示行数（1–9） |
| `autoStart` | bool | `true` | 开机自启（注册表 Run 键，托盘可切换） |
| `excludeHidden` | bool | `true` | 索引时排除隐藏文件 |
| `excludeSystem` | bool | `true` | 索引时排除系统文件 |
| `providers.file.enabled` | bool | `true` | 启用文件搜索（NTFS USN） |
| `providers.bookmark.enabled` | bool | `true` | 启用浏览器书签搜索 |
| `providers.app.enabled` | bool | `true` | 启用开始菜单应用搜索 |

> 完整字段说明见 [`docs/configuration.md`](docs/configuration.md)（编写中）。

---

## 3. 贡献引导

### 贡献流程

欢迎 Issue 与 PR！请遵循 [`CONTRIBUTING.md`](CONTRIBUTING.md) 的规范：

- **Issue**：Bug 报告请附复现步骤与 `Iris.exe` 同级 `log/iris_*.log`；功能建议请描述使用场景。
- **PR**：须通过全部 `ctest`（当前 45 项），提交信息遵循 [约定式提交](https://www.conventionalcommits.org/zh-hans/)（`feat:` / `fix:` / `docs:` …），改动聚焦、不夹带无关重构。

### 本地开发

```powershell
git clone https://github.com/AomeNero/Iris.git
cd Iris
scripts\run-in-vcvars.cmd cmake -B build -Wno-dev                                  # 配置
scripts\build.cmd                                                                  # 构建
scripts\run-in-vcvars.cmd ctest --test-dir build -C Release --output-on-failure    # 测试
.\build\Release\Iris.exe                                                           # 运行
```

项目分层架构（依赖单向自下而上）：`Core ← Provider ← Engine ← UI ← App`，跨层通信统一走 Qt Signal/Slot。动手前建议先读 [`CLAUDE.md`](CLAUDE.md) 的「关键设计约束」一节。

### 社区渠道

| 渠道 | 链接 |
|------|------|
| GitHub Discussions | [前往讨论区](https://github.com/AomeNero/Iris/discussions) |

---

## 4. 元信息与信任背书

### 致谢

本项目站在巨人的肩膀上，核心依赖：

- [Qt 5.14.2](https://www.qt.io/) — 跨平台 GUI 框架与 QPainter 自绘
- [nlohmann/json](https://github.com/nlohmann/json) — 现代 C++ JSON 库
- [SQLite](https://www.sqlite.org/) — 嵌入式数据库（历史记录存储）
- [GoogleTest](https://github.com/google/googletest) — 单元测试框架

**Contributors**

<a href="https://github.com/AomeNero/Iris/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=AomeNero/Iris" alt="contributors"/>
</a>

### 开源协议

本项目基于 [**MIT License**](./LICENSE) 开源，© 2026 [AomeNero](https://github.com/AomeNero)。

```
 Iris — Windows 极速桌面启动器
 Copyright (c) 2026 AomeNero <yotianya@gmail.com>
```

自由使用、修改与分发，欢迎 ⭐ Star 支持！
