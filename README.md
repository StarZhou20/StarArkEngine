# StarArkEngine v0.1-renderer

> 一个**可被 AI 直接调用**的 C++20 PBR 渲染后端。
> 不是完整游戏引擎，不是开放世界平台——只是一个把"场景描述 C++ 代码"渲染成像素的后端。

---

## 它是什么

StarArkEngine v0.1 给你以下能力，**仅此而已**：

- OpenGL 4.5 窗口 + 键鼠输入 + 日志 + 路径工具
- AScene / AObject / AComponent 的组件式场景
- Forward PBR（Cook-Torrance）+ 多贴图（albedo/normal/MR/AO/emissive）
- 方向光 / 点光 / 聚光灯 + 物理光衰减
- IBL（irradiance + prefilter + BRDF LUT 运行时烘焙）
- 方向光 shadow map + 5×5 PCF
- HDR FBO + Bloom + ACES Hill tonemap
- Skybox（cubemap 或程序化渐变）
- Assimp（OBJ/FBX/glTF） + stb_image（PNG/JPG/...）
- Shader 文件热重载（改 `.vert/.frag` 立即生效）
- 外部 JSON 调光（`content/lighting.json` + 配套 WPF Lighting Tuner 工具）

## 它**不是**什么

v0.1 **没有**：脚本语言、场景对象序列化、Prefab、编辑器、物理、骨骼动画、动画状态机、
音频、网络、UI 系统、粒子、CSM、点光阴影、DX12 后端、GPU Instancing、多线程渲染。

这些**故意不做**。详见 [docs/Capabilities.md](docs/Capabilities.md) 和 [docs/KnownIssues.md](docs/KnownIssues.md)。

长期愿景（Y+ 开放数据契约、AI 驱动开放世界）在 [docs/Roadmap.md](docs/Roadmap.md)，**不属于 v0.1**。

---

## 1 分钟上手

1. 构建（Windows + MSVC）:

    ```powershell
    cmd /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake -S . -B build -G \"NMake Makefiles\" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && cmake --build build"
    ```

2. 运行样例:

    ```powershell
    build\samples\StarArkSamples.exe            # 默认 DemoScene（PBR 球阵）
    build\samples\StarArkSamples.exe cottage    # v0.1 最小自洽 demo（只用公共 API）
    build\samples\StarArkSamples.exe fbx        # FBX 模型演示
    ```

3. 写自己的 demo: 复制 `game/main.cpp`，继承 `ark::AScene`，在 `OnLoad()` 里创建对象。

完整入门模板见 [docs/Capabilities.md §4](docs/Capabilities.md)。

---

## 构建环境要求

| 项目 | 要求 |
|------|------|
| 系统 | Windows 10/11（v0.1 仅测 Windows；Linux/macOS 待 v0.2+） |
| 编译器 | MSVC 19.50+（Visual Studio 2025 Community 或更新） |
| CMake | 4.0+ |
| 生成器 | NMake Makefiles（默认，与工程的 POST_BUILD 脚本一致） |
| 网络 | 首次配置时 FetchContent 需要 `deps_cache/` 里的本地 zip；代理地址见 [docs/DevLog.md §2](docs/DevLog.md) |
| 权限 | 首次配置需**管理员权限或开发者模式**（`mklink /J` 给 glm-light 建 junction，见 [docs/KnownIssues.md C1](docs/KnownIssues.md)） |

### 详细命令

```powershell
# 1. 配置（仅首次 / 改 CMakeLists.txt / 新增 .cpp 文件后）
cmd /c "call \"<VS安装路径>\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake -S . -B build -G \"NMake Makefiles\" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# 2. 编译（日常开发）
cmd /c "call \"<VS安装路径>\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake --build build"

# 3. 清理重建
Remove-Item build -Recurse -Force; cmake -S . -B build ...
```

工程使用 `FetchContent` + 本地 zip 依赖：GLFW 3.4 / GLM 1.0.1 / GLEW 2.2.0 / spdlog 1.15.0 / Assimp 5.4.3。详见 [docs/DevLog.md §2](docs/DevLog.md)。

构建产物：

- `build\engine\StarArkEngine.lib` — 引擎静态库
- `build\samples\StarArkSamples.exe` — 演示场景
- `build\game\StarArkGame.exe` — SDK 空壳
- 可执行文件旁自动 POST_BUILD 拷贝 `content/` + `content/shaders/`

---

## AI / 新开发者文档阅读顺序

读这四份文件，**不用问作者任何问题**就能写出可运行的 demo：

1. [docs/Capabilities.md](docs/Capabilities.md) — 能做什么 / 不能做什么 / 需要什么输入（一页纸）
2. [docs/API.md](docs/API.md) — 公共 API 参考（签名在 v0.1 tag 后冻结）
3. [docs/KnownIssues.md](docs/KnownIssues.md) — v0.1 的已知边界（避免踩 v0.2+ 坑）
4. [docs/DevLog.md](docs/DevLog.md) — 工程历史 + 构建环境细节

长期方向（与当前代码无关）: [docs/Roadmap.md](docs/Roadmap.md)

---

## 仓库结构

```
engine/           # StarArkEngine 静态库（内核）
  core/           # EngineBase / Scene / AObject / AComponent / Transform
  platform/       # Window / Input / Time / Paths
  rhi/            # RHI 抽象 + OpenGL 后端
  rendering/      # Forward / PBR / Shadow / IBL / Bloom / Skybox / ShaderManager / SceneSerializer
  debug/          # 日志
  shaders/        # 运行时 GLSL 文件（POST_BUILD 拷到 exe 旁）
game/             # StarArkGame.exe —— SDK 空壳，作为玩家项目起点
samples/          # StarArkSamples.exe —— 所有演示场景
tools/
  LightingTuner/  # WPF .NET 10 外部调光工具，只编辑 lighting.json
docs/             # 本文件引用的所有文档
```

---

## 许可

（待定）
