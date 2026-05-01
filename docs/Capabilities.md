# StarArk v0.1-renderer Capabilities

> **读者**: 不熟悉本工程的 AI 编码助手 / 开发者。
> **目的**: 一页讲清楚 v0.1 **能做什么 / 不能做什么 / 需要什么输入**，让你不用问作者就能写代码。
> 精确的函数签名请看 [API.md](API.md)；历史实现记录见 [DevLog.md](DevLog.md)。
>
> **v0.3 起的 mod 范式 / 数据契约规则**见 [ModSpec.md](ModSpec.md)（自包含 mod / 三轨路径 / 双层持久身份 / ScriptApi v3 等）。本文件只描述 v0.1-renderer 已冻结的渲染后端能力。

---

## 1. 一句话定位

**StarArkEngine v0.1 是一个 C++20 PBR 渲染后端**，通过 `EngineBase::Run<YourScene>()` 启动，
在 `AScene::OnLoad()` 里用 C++ 代码创建对象/组件来描述场景，然后引擎负责把它绘制出来。

没有脚本语言，没有编辑器，没有资产数据库。**调用方式 = 写 C++ 代码继承 `AScene` + `AObject`**。

---

## 2. 能做什么（v0.1 完整能力清单）

### 2.1 窗口与输入（`ark::`）

- GLFW 窗口 + OpenGL 4.5 Core 上下文（sRGB framebuffer）
- 键盘：`Input::GetKey / GetKeyDown / GetKeyUp`
- 鼠标：`Input::GetMouseButton(Down/Up)`、`GetMousePosition / GetMouseDeltaX|Y`、`GetScrollDelta`
- 时间：`Time::DeltaTime() / TotalTime() / FrameCount()`
- 日志：`ARK_LOG_INFO/WARN/ERROR/FATAL(category, message)` 两参宏，控制台彩色 + 文件轮转

### 2.2 场景与对象系统

- `AScene`：`OnLoad()` / `Tick(dt)` / `OnUnload()`
- `AObject`：内置 `Transform`，生命周期 `PreInit → Init → PostInit → Loop → PostLoop → OnDestroy`
- `AComponent`：`OnAttach / OnDetach / PreInit / Init / PostInit / Loop / PostLoop`
- `CreateObject<T>()` / `AddComponent<T>()` 返回裸指针，所有权留在父级
- 父子 Transform 层级 + 脏标记（只遍历脏子树）
- `SetDontDestroy(true)` 跨场景保留对象
- `SceneManager::LoadScene<T>()` 延迟切换到帧末

### 2.3 渲染功能

| 能力 | 细节 |
|------|------|
| Forward PBR | Cook-Torrance（GGX + Smith + Fresnel-Schlick）；metallic-roughness 工作流 |
| Deferred PBR | v0.2.x 落地：4-RT G-buffer + Depth32F + 全屏 Lighting Pass；`RenderSettings::pipeline = Forward \| Deferred` 切换；透明走 forward overlay 叠在 lit_ 上 |
| 多光源 | 最多 **4 方向光 + 8 点光 + 4 聚光灯**（shader 数组硬上限） |
| 物理光衰减 | 点光/聚光 1/r² + 软截断（`range` 外淡出） |
| 多贴图 PBR | albedo / normal（TBN）/ MR（G=rough, B=metal）/ AO（R）/ emissive |
| HDR + Bloom | RGBA16F 场景 FBO；半分辨率亮度提取 + 可分离高斯 ping-pong（`iterations` 可调） |
| Tonemap | **ACES Hill fitted**（线性 HDR → 线性 LDR），交给 `GL_FRAMEBUFFER_SRGB` 做最终编码 |
| Skybox | cubemap；无贴图时自动填充程序化天顶-地平-地面 HDR 渐变 |
| IBL | 运行时烘焙：irradiance 32³ + prefilter 128³×5mip + BRDF LUT 512²（启动后首帧烘焙一次） |
| 方向光阴影 | 单层 shadow map（默认 2048²，正交投影）+ 5×5 PCF；仅对第一盏启用的方向光投影 |
| Shader 热重载 | 改 `content/shaders/*.{vert,frag}` 存盘即重编；失败保留旧 program |
| 运行时调参 | `content/lighting.json`（mtime 热重载）；外部工具 WPF Lighting Tuner 可视化 |

### 2.4 资源加载

| 类型 | 支持 | 代码入口 |
|------|------|---------|
| 图片纹理 | PNG / JPG / BMP / TGA（stb_image），16× 各向异性，可标 sRGB/Linear | `TextureLoader::Load(device, path, isSRGB)` |
| 3D 模型 | OBJ / FBX / glTF（Assimp 5.4.3，triangulate + gen normals + calc tangent + flip UV） | `ModelLoader::Load(device, shader, path)` → `vector<ModelNode>` |
| Shader | 运行时从 `content/shaders/<name>.vert|.frag` 读取，fallback 到嵌入源 | `shaderManager->Get("pbr" | "phong" | "depth")` |

### 2.5 路径 / 部署

- `Paths::Init(argv[0])` 必须 `main()` 第一行
- `Paths::ResolveContent("models/foo.obj")` → 绝对路径（exe 旁的 `content/`）
- v0.3 起 mod 资源应改用三轨语法 `./` / `mod://` / `engine://`（[ModSpec.md §3](ModSpec.md)）；裸路径 fallback 仍可工作但发 deprecation WARN
- Build 已配置 POST_BUILD 拷 `engine/shaders/` + 样例 `content/` 到 exe 旁
- 可执行文件：`build/game/StarArkGame.exe`（空壳）、`build/samples/StarArkSamples.exe`（演示场景）

---

## 3. 不能做什么（v0.1 明确排除）

| 不做 | 理由 / 什么时候有 |
|------|------------------|
| **脚本语言**（C# / Lua / Python） | ✅ v0.2.x：C# (CoreCLR / .NET 10) MOD 系统已上线；详见 [DevLog Phase 15.F](DevLog.md)。Lua/Python 不计划。 |
| **Prefab / 预制体** | 没有，手写构造 |
| **运行时 Inspector / GUI 编辑器** | 没有；外部 WPF Lighting Tuner 只编辑 `lighting.json` |
| **物理 / 碰撞检测** | 没有 `RigidBody` / `Collider` / 射线查询 |
| **骨骼动画 / 蒙皮** | 没有；Assimp 导入时骨骼数据会被丢弃 |
| **动画状态机** | 没有 |
| **音频** | 没有 |
| **网络 / 多人** | 没有 |
| **UI 系统** | 没有文本渲染、按钮、布局；任何 2D UI 都得自己用 MeshRenderer 画四边形 |
| **级联阴影（CSM）/ 点光阴影 / 软阴影** | 只有单层方向光 shadow map + PCF |
| **DX12 后端** | RHI 抽象预留，但只实现了 OpenGL |
| **多线程渲染** | 主线程录制 + 主线程提交 |
| **GPU Instancing** | 每个 MeshRenderer 一次 DrawCall |
| **资源管理器 / hot-swap 纹理** | 没有 `ResourceManager`；Material/Texture/Mesh 用 `shared_ptr` 手动共享 |
| **粒子系统 / 体积光 / SSR / SSAO / TAA** | 没有 |

---

## 4. 需要什么输入

### 4.1 入口代码骨架

```cpp
#include "engine/core/EngineBase.h"
#include "engine/platform/Paths.h"

class MyScene : public ark::AScene {
public:
    void OnLoad() override { /* create objects here */ }
    void Tick(float dt) override {}
    void OnUnload() override {}
};

int main(int argc, char** argv) {
    ark::Paths::Init(argv[0]);
    return ark::EngineBase::Get().Run<MyScene>(1280, 720, "MyGame");
}
```

### 4.2 典型场景构造模板

在 `MyScene::OnLoad()` 里：

```cpp
// 1. 相机 —— 必须有至少一个，否则不渲染
auto* camObj = CreateObject<ark::AObject>();
auto* cam    = camObj->AddComponent<ark::Camera>();
cam->SetPriority(0);
camObj->GetTransform().SetLocalPosition({0, 2, 5});

// 2. 光 —— 至少一盏方向光，否则场景全黑（除非开 IBL）
auto* lightObj = CreateObject<ark::AObject>();
lightObj->SetName("Sun");              // SceneSerializer 按 name 匹配
auto* light = lightObj->AddComponent<ark::Light>();
light->SetType(ark::Light::Type::Directional);
light->SetIntensity(3.0f);
lightObj->GetTransform().SetLocalRotation(glm::quat(glm::vec3(-0.7f, 0.3f, 0)));

// 3. 网格 + 材质 + 渲染器
auto* device = ark::EngineBase::Get().GetRHIDevice();
auto* shaderMgr = ark::EngineBase::Get().GetRenderer()->GetShaderManager();
auto  shader = shaderMgr->Get("pbr");

auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateSphere(48, 24).release());
mesh->Upload(device);

auto mat = std::make_shared<ark::Material>();
mat->SetShader(shader);
mat->SetColor({0.8f, 0.1f, 0.1f, 1.0f}); // albedo (RGBA, v0.1 无 SetAlbedo)
mat->SetMetallic(0.0f);
mat->SetRoughness(0.3f);

auto* cubeObj = CreateObject<ark::AObject>();
auto* mr      = cubeObj->AddComponent<ark::MeshRenderer>();
mr->SetMesh(mesh);
mr->SetMaterial(mat);

// 4.（可选）启用运行时调参文件
ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));
```

### 4.3 目录约定

exe 旁的 `content/` 结构：

```
content/
├── shaders/         # pbr.vert/.frag, phong.vert/.frag, depth.vert/.frag（POST_BUILD 自动拷）
├── models/          # 你的 OBJ/FBX/glTF
├── textures/        # 你的 PNG/JPG（外部贴图路径相对于模型文件）
└── lighting.json    # 可选；启用热重载后引擎启动首帧自动生成
```

相对路径通过 `Paths::ResolveContent("...")` 解析。

### 4.4 Light 命名约定（若用 SceneSerializer）

- `lighting.json` 里的光源按 **`AObject::GetName()`** 匹配回 runtime
- 未命名的光源不会被持久化（不会出现在 JSON 里）
- JSON 里未匹配的条目被忽略；runtime 里 JSON 没有的光源保留初始值

---

## 5. 运行时参数默认值

（与 `RenderSettings.h` 和 `Light.h` 一致；也是 WPF Lighting Tuner 的 reset 默认）

| 字段 | 默认 |
|------|------|
| `exposure` | 1.0 |
| `bloom.threshold / strength / iterations` | 1.0 / 0.6 / 5 |
| `shadow.resolution / orthoHalfSize / depthBias / normalBias / pcfKernel` | 2048 / 25.0 / 0.002 / 0.010 / 2 |
| `ibl.diffuseIntensity / specularIntensity` | 1.0 / 1.0 |
| `Light.intensity / range` | 1.0 / 10.0 |
| `Light.spotInnerAngle / spotOuterAngle` | 12.5° / 17.5° |

---

## 6. 写 demo 时的最小检查清单

- [ ] 第一行调 `Paths::Init(argv[0])`
- [ ] `main()` 调 `EngineBase::Get().Run<YourScene>(w, h, title)`
- [ ] `OnLoad` 至少创建 1 个 `Camera` + 1 个 `Light`（否则黑屏）
- [ ] 每个被 `lighting.json` 调参的光源都 `SetName("...")`
- [ ] 贴图路径用 `Paths::ResolveContent("textures/foo.png")`
- [ ] sRGB 贴图（albedo/emissive）`isSRGB=true`；数据贴图（normal/MR/AO）`isSRGB=false`
- [ ] Shader 全部用 `shaderManager->Get("pbr")`，**不要** 手写 `device->CreateShader() + Compile()`
- [ ] 新增 .cpp 记得加到 `engine/CMakeLists.txt` GLOB_RECURSE 覆盖的目录

满足以上，一次启动就能出画面。
