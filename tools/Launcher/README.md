# StarArk Launcher

WPF (.NET 10, Windows-only) 启动器。深色主题，扫描同目录 `mods/`，列出所有 game / addon mod，
让玩家选择启动。启动后启动器自身退出。

## 设计要点

- **同目录部署**：发行时把 `bin/Release/net10.0-windows/` **整个文件夹**复制到任意"游戏文件夹"
  （比如 `samples/` 或玩家分发包根目录）。启动器只会扫自己 .exe 旁边的 `mods/` 目录，**不做任何路径回退**。
- **运行时 exe 名可配置**：`launcher.config.json`（与启动器 .exe 同目录）里 `RuntimeExecutable` 字段
  指向真正的引擎二进制（默认 `StarArkSamples.exe`）。换游戏时只需重命名 exe + 改这个字段，
  无需重新编译启动器。
- **CLI 契约**：启动器调用引擎时传：
  ```
  <RuntimeExecutable> --game=<id> --pipeline=<forward|deferred> [--addon=<id>]...
  ```
  这是启动器与引擎之间唯一的协议。引擎那边由 `samples/src/main_samples.cpp` 解析（见 ModSpec §0）。
- **不嵌 CoreCLR**：启动器是独立 .NET 10 WPF 进程，引擎仍是原生 C++ 二进制。
- **不下载/安装 mod**：v0.3 启动器是纯本地 mod 选择器。Workshop / Nexus 集成是 v0.5+ 的事。

## 项目结构

```
tools/Launcher/
  Launcher.csproj
  app.manifest          ← Per-Monitor V2 DPI
  AssemblyInfo.cs
  App.xaml / .cs
  MainWindow.xaml / .cs
  Themes/
    Dark.xaml           ← 深色主题资源
  Models/
    LauncherConfig.cs   ← 读 launcher.config.json
    ModInfo.cs          ← 解析后的 mod.toml
  Services/
    ModScanner.cs       ← 扫 mods/ + 解析（Tomlyn）
    Validator.cs        ← engine_min / pipeline / applies_to / depends_on
    LaunchService.cs    ← Process.Start 调引擎
  ViewModels/
    MainViewModel.cs
  launcher.config.sample.json
```

## 构建与部署

```powershell
# 在 tools\Launcher\ 目录下
dotnet build -c Release

# 输出位于 bin\Release\net10.0-windows\
# 把整个文件夹复制到你的游戏目录，例如：
Copy-Item bin\Release\net10.0-windows\* <GameFolder>\ -Recurse

# 在 <GameFolder> 下放一份 launcher.config.json：
Copy-Item launcher.config.sample.json <GameFolder>\launcher.config.json
```

## 与 ModSpec 对齐情况（v0.3）

| ModSpec 节 | 启动器实现 |
|---|---|
| §1 game / addon 类型 | 左 game 单选 / 右 addon 多选 |
| §2.1 必需字段 | 缺失则该 mod 红字标错、不可启动 |
| §2.2 可选字段 | 全部解析；未实装显示但保留数据 |
| §3 三轨路径 | 启动器**不解析**，由引擎处理 |
| §4 持久 ID | 启动器**不解析**，由引擎处理 |
| §5 overlay | 启动器只决定加载顺序，由引擎应用 |
| §6 存档 | 启动器**不涉及** |

## 已知限制（v0.3）

- 引擎版本 / ScriptApi 版本目前是常量（`MainViewModel.EngineVersionStub`）。
  v0.4 改为调 `<RuntimeExecutable> --version` 取真实值。
- addon `load_after` / `load_before` 当前未做拓扑排序，按字母序传给引擎。
  多 addon 冲突时由引擎按 ModSpec §5.2 处理。
- Steam Workshop / Nexus / 自动更新均未实现。
