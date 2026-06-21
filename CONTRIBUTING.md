# 贡献指南

感谢你对 Iris 的兴趣！🎉 无论是提交 Issue、修复 Bug、新增功能还是改进文档，都非常欢迎。本指南帮助你快速参与本项目。

## 目录

- [行为准则](#行为准则)
- [开发环境](#开发环境)
- [本地开发流程](#本地开发流程)
- [代码规范](#代码规范)
- [提交规范](#提交规范)
- [Issue 指南](#issue-指南)
- [Pull Request 流程](#pull-request-流程)
- [项目结构](#项目结构)
- [交流](#交流)

---

## 行为准则

请保持友善与尊重，对技术水平不同的贡献者一视同仁。技术讨论聚焦问题本身，不针对个人。

## 开发环境

| 项 | 要求 |
|----|------|
| 操作系统 | Windows 10 (1903+) / Windows 11 |
| 编译器 | MSVC（Visual Studio 2022 / 2026 工具集，`cl.exe`） |
| GUI 框架 | Qt 5.14.2 **静态库**（Core / Gui / Widgets / Svg） |
| 构建系统 | CMake ≥ 3.16 |
| 运行时 | 静态链接 CRT（`/MT`），无外部运行时依赖 |

> ⚠️ 项目针对 **Qt 5.14.2 静态版**开发，不兼容 Qt6。MSVC 与 Qt 5.14.2 的若干已知不兼容点已在源码中修复（见 [`CLAUDE.md`](CLAUDE.md)「环境约定」），勿擅自改动。

## 本地开发流程

```powershell
# 1. 克隆
git clone https://github.com/AomeNero/Iris.git
cd Iris

# 2. 配置（首次或 CMakeLists 改动后）
scripts\run-in-vcvars.cmd cmake -B build -Wno-dev

# 3. 增量构建（Release）
scripts\build.cmd

# 4. 运行全部测试
scripts\run-in-vcvars.cmd ctest --test-dir build -C Release --output-on-failure

# 5. 运行
.\build\Release\Iris.exe
```

> 💡 `cl.exe` 不在 PATH 时，所有需要 MSVC 的命令都通过 `scripts\` 下的 `.cmd` 包装器自动加载 vcvars。**这些 `.cmd` 必须保持纯 ASCII**（中文系统下 cmd.exe 按 GBK 解析 `.bat`，含中文/UTF-8 注释会导致乱码与死循环）。

## 代码规范

- **语言标准**：C++17。优先使用现代特性（`auto`、`constexpr`、智能指针、`std::optional`），RAII 管理资源。
- **分层架构**（依赖单向自下而上）：`Core ← Provider ← Engine ← UI ← App`
  - Core 不得依赖上层；Provider 之间互不依赖，也不感知 Engine；Engine 不关心数据来源与展示；UI 不关心搜索逻辑。
  - 跨层通信**统一走 Qt Signal/Slot**（跨线程用 `Qt::QueuedConnection`），不直接 include 内部实现头文件。
- **硬性约束**：动手前务必阅读 [`CLAUDE.md`](CLAUDE.md) 的「关键设计约束」一节（如 `CompactFileEntry` 必须 14 字节、CoW 索引更新、跨线程 Lambda 值捕获等），违反将阻塞代码审查。
- **风格**：命名用 `snake_case_`（成员变量尾随下划线）、类型 `PascalCase`、常量 `kPascalCase`；单行注释**禁止以 `\` 结尾**。

## 提交规范

遵循[约定式提交](https://www.conventionalcommits.org/zh-hans/)：

```
<type>: <description>

<可选正文>
```

常用 type：`feat`（新功能）、`fix`（修复）、`refactor`（重构）、`perf`（性能）、`docs`（文档）、`test`（测试）、`build`（构建）、`chore`（杂项）。

- 描述用中文或英文均可，但需简明扼要、说明**为什么**改。
- 一个提交只做一件事，改动聚焦，不夹带无关重构。

## Issue 指南

提交 Issue 前请先搜索是否已存在相同问题。

**Bug 报告**请包含：
- 复现步骤（尽可能精确）
- 预期行为 vs 实际行为
- 运行环境（Windows 版本、是否便携部署、是否管理员权限）
- `Iris.exe` 同级 `log/iris_*.log` 的相关日志

**功能建议**请描述：
- 使用场景与要解决的痛点
- 期望的交互形态（可附草图）

## Pull Request 流程

1. **Fork** 仓库并基于 `main` 创建特性分支：`git checkout -b feat/your-feature`
2. **开发**：遵循上述代码规范，新功能请同步编写单元测试。
3. **本地验证**：确保 `scripts\build.cmd` 干净构建、`ctest` 全部通过（新增代码测试覆盖率 ≥ 80%）。
4. **提交**：遵循提交规范，可包含多个聚焦的小提交。
5. **发起 PR**：在描述中说明「改了什么 / 为什么改 / 如何验证」，关联相关 Issue（如 `Closes #12`）。
6. **代码审查**：响应审查意见，按需追加提交（不要 force-push 打乱时间线，除非审查者要求）。

**合并标准**：
- ✅ 全部 `ctest` 通过、构建无警告
- ✅ 符合分层架构与硬性约束
- ✅ 新功能有测试覆盖
- ✅ 提交信息规范、改动聚焦

## 项目结构

```
src/
├── core/       Config、Logger、ThreadPool、WinUtil、HistoryStore、HotkeySpec
├── provider/   IProvider + FileProvider / BookmarkProvider / AppProvider
├── engine/     QueryParser、Matcher、Ranker、SearchEngine
├── ui/         SearchWindow、SearchBar、ResultList、HotkeyManager、TrayIcon、Theme
└── main.cpp    入口、依赖注入、生命周期
test/           GoogleTest 单元测试（被测源码直接编译进测试 target）
doc/            需求、详细设计、编码编排、分模块任务清单
resources/      iris.png / iris.ico / ctrl.png / enter.png
third_party/    nlohmann/json、sqlite3（amalgamation）、googletest
scripts/        build.cmd / run-in-vcvars.cmd / build-manual.cmd
```

测试约定：被测 `src/*.cpp` 直接编译进测试 target（Iris 是 WIN32 GUI 程序含 WinMain，不能与测试 main 共存）；在 `test/CMakeLists.txt` 用 `add_iris_test(<name> <srcs>... [LINK <libs>])` 注册。

## 交流

- 💬 [GitHub Discussions](https://github.com/AomeNero/Iris/discussions) — 功能讨论、使用问答、想法交流
- 🐛 [GitHub Issues](https://github.com/AomeNero/Iris/issues) — Bug 报告与功能跟踪

---

再次感谢你的贡献！每一份 Issue 和 PR 都让 Iris 更好。🌸
