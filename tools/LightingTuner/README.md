# StarArk Lighting Tuner (WPF)

独立的 WPF 调光工具，与 StarArkEngine 本体**零代码耦合**——仅通过
`content/lighting.json` 通讯。

## 直接使用（已构建）

已构建好的发行产物位于：

```
tools/LightingTuner/bin/Release/net10.0-windows/win-x64/publish/StarArkLightingTuner.exe
```

双击即可。启动时会从当前目录向上追溯 8 层，在 `build\samples\content\lighting.json`
等几处默认位置里找 JSON；找不到就弹 *Browse…* 让你挑。

## 自己构建

前置：.NET 10 SDK（`dotnet --list-sdks` 应能看到 `10.x`）

```powershell
cd tools\LightingTuner
dotnet build -c Release
# 生成位置：bin\Release\net10.0-windows\StarArkLightingTuner.exe
```

发行为**单文件 exe**（~180 KB，依赖系统上的 .NET 10 Runtime）：

```powershell
dotnet publish -c Release -r win-x64 `
  -p:SelfContained=false `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true
```

想生成**不依赖 .NET 运行时**的完全独立 exe（约 130 MB）：把 `-p:SelfContained=false`
去掉即可。

## 运行时流程

1. `StarArkSamples.exe` 启动，`SceneSerializer::EnableHotReload` 监视
   `content/lighting.json` 的 mtime。
2. Lighting Tuner 读取同一 JSON，用 WPF 滑块/色卡编辑数值。
3. *Auto-apply* 默认开启，拖动滑块即刻原子写回（`.tmp` + `File.Move`）。
4. 引擎下一帧检测到 mtime 变化，`Load` 进 runtime，画面即时更新。

反向也成立：引擎启动时若 JSON 被它重写，工具会在 0.5s 轮询里发现并自动 Reload UI。

## 可调项

- **Render tab**: `exposure`、`bloom`、`sky`、`ibl`、`shadow`（含 orthoHalfSize /
  depthBias / normalBias / pcfKernel）
- **每盏光一个 tab**: `color` (RGB + swatch 预览)、`intensity`、`ambient`、
  `position` (Point/Spot)、`rotationEuler` (Directional/Spot)、`range` / `constant`
  / `linear` / `quadratic`、`innerAngle` / `outerAngle` (Spot)

## 不做什么

- 不加光 / 不删光（那是场景代码的职责，走 AScene::CreateObject）
- 不改 mesh / material（Mini-M10 只序列化光 + RenderSettings）

## 为什么选 WPF

- Windows 平台原生，无额外运行时依赖（系统已装 .NET 10 时 180 KB exe）
- XAML 布局 + `System.Text.Json.Nodes` 直接操作 JSON，约 450 行 C# 足矣
- 单文件可打包；不污染引擎 C++ 构建
