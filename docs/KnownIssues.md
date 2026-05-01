# StarArk v0.1-renderer Known Issues & Technical Debt

> 用来告知 AI / 新开发者：**这些"问题"不是你的 bug，是 v0.1 已知边界**。
> 修它们都不属于 v0.1 范围；留给 v0.2+。
> 新发现的问题请追加到本文件对应分类下。

---

## A. 硬性能力边界（不是 bug，是没实现）

见 [Capabilities.md §3](Capabilities.md)。以下项在 v0.1 中**故意不实现**，遇到需求直接说"v0.1 做不了"：

- 脚本语言 / Prefab / 运行时 Inspector
- 物理 / 碰撞 / 射线查询
- 骨骼动画 / 蒙皮 / 动画状态机
- 音频 / 网络 / UI 系统 / 粒子
- 级联阴影、点光阴影、软阴影高级算法
- DX12 / Vulkan 后端
- GPU Instancing / 多线程渲染
- 通用场景对象序列化（只序列化 RenderSettings + Light）

---

## B. 架构遗留（v0.1 内不修）

### B1. Uniform 设置绕过 RHI CommandBuffer

`ForwardRenderer` 在 `cmdBuffer->Begin()/End()` 之间直接调用 `shader->SetUniform*()`，本质是立即模式 OpenGL 调用，没有进入命令缓冲区的录制机制。这在 OpenGL 后端工作正常，但：

- 不符合 RHI 的"录制-回放"语义
- 将来做 DX12 后端时必须重构为"通过 CommandBuffer 写 uniform buffer"
- 不能在非主线程预录制命令

**缓解**: v0.1 暂不做多线程/DX12，先维持现状。DX12 后端立项时一并重做。

### ~~B1.5. RHI 缺 RenderTarget / MRT / 完整纹理格式~~ ✅ 2026-04-26 完结

`RHIRenderTarget` 抽象 + MRT 4-RT G-buffer + Depth32F/RGBA16F/RG16F 格式枚举均已落地（v0.2.x 延迟管线前置）。`PostProcess` / `IBL` 内剩余 raw `glGenFramebuffers` 是 follow-up，不阻塞 deferred 落地。详见 [Roadmap.md "延迟渲染前置工作清单"](Roadmap.md)。

### ~~B1.6. Material 单 shader 假设 / 缺 RenderQueue~~ ✅ 2026-04-26 完结

`Material::BindToShader(RHIShader*)` 显式绑（deferred 喂 gbuffer / forward 喂 pbr）；`Material::IsTransparent()` + `DrawListBuilder` 双桶完成 opaque/transparent 路由。

### B2. 静态注册表非线程安全

`Camera::allCameras_` / `Light::allLights_` / `MeshRenderer::allRenderers_` 都是静态 `std::vector<T*>`，由 `OnAttach`/`OnDetach` 增删。所有 Tick/渲染逻辑跑在主线程，当前没有竞态——但任何并发化尝试（多线程 Tick / 异步加载）都会踩坑。

**缓解**: v0.1 不并发化。

### B3. `RenderSettings` 字段分散在 shader uniform 里

JSON 保存 / Lighting Tuner / `RenderSettings.h` / shader uniform / `ForwardRenderer` 成员，这五处是手工同步的。加一个新字段要改 5 个地方。

**缓解**: v0.1 不做反射；新字段需要作者按规矩人工改全五处。v0.2+ 的反射系统解决。

### B4. Material 参数不走 shared uniform buffer

每个 DrawCall 单独 `glUniform*`，没有 UBO/SSBO。对 v0.1 的样例级数量（几十到几百物体）足够。

### B5. 阴影仅覆盖"第一个启用的方向光"

代码逻辑写死 `i == 0` 才计算 shadow。第二盏方向光 / 点光 / 聚光灯都不投影。

**缓解**: 场景设计时让"太阳"是第一个启用的方向光即可。

### B6. `SceneSerializer` 是零依赖手写 JSON

~150 行的手写 parser 只支持够用的子集（嵌套对象 / 数组 / 字符串 / 数字 / bool）。给它非常刁钻的 JSON（注释、尾逗号、Unicode 转义）会解析失败。

**缓解**: 外部工具生成的 JSON 保持 ASCII + 无注释即可。

### ~~B7. Light 匹配靠 `AObject::GetName()` 字符串相等~~ ✅ v0.3 完结

持久身份已升级到 `"<mod_id>:<local_id>"` 扁平字符串（[ModSpec.md §4.2](ModSpec.md) + `engine/core/PersistentId.h`）。`SceneDoc` 走 GUID 匹配，重名不再有歧义。`SceneSerializer`（已废弃，仅 lighting.json 仍用名字）不在主链路上。

### B8. IBL 只烘焙一次

启动后首帧拿 skybox 做一次预计算；运行时换 skybox → 需要手动 `renderer->RebakeIBL()`。没有自动失效/重烘。

---

## C. 构建 / 环境

### C1. GLM include 依赖 junction

构建脚本通过 `mklink /J` 给 glm-light 造目录结构，**首次构建需要管理员权限或开发者模式**。

### C2. GLEW 需要 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`

否则 `glew-cmake` 的过旧 `cmake_minimum_required` 导致配置失败。已在构建命令里带上。

### C3. 依赖下载用本地 zip 缓存

`deps_cache/` 下的 zip 缺失会触发 FetchContent 联网下载；代理不稳定会失败。zip 名字和版本见 [DevLog §2](DevLog.md)。

### C4. POST_BUILD 拷 `content/` 依赖目录已存在

如果 `samples/content/` 或 `engine/shaders/` 目录被手动删掉，cmake 需要重跑配置；POST_BUILD 的 `copy_directory` 只在目录存在时执行。

### C5. 仅测 MSVC 19.50 + Windows

不保证 clang / GCC / Linux 可构建。跨平台属于 v0.2+ 工作。

---

## D. 性能 / 规模边界

| 场景 | 估算能承受 | 怎么爆 |
|------|-----------|-------|
| MeshRenderer 数量 | ~500 级别 | 每个一次 DrawCall + uniform 上传，千级开始掉帧 |
| 光源数量 | Shader 上限 4 Dir / 8 Point / 4 Spot | 超过会被 shader 忽略（不会崩，但多的不渲染） |
| Shadow map 分辨率 | 2048² 默认 | 调到 8192² 以上会显著吃带宽（单线程阴影 pass） |
| IBL Prefilter | 128³ / 5 mip | 烘焙期约 50–200 ms，启动阻塞主线程 |
| Bloom 迭代 | 5 对 ping-pong | 调到 10+ 对后，半分辨率模糊也会掉帧 |

---

## E. 已知 bug（待修，不阻塞 v0.1 tag）

> 目前无阻塞 bug。发现时追加：

- *（空）*

---

## F. 文档债务

- [ ] `docs/API.md` 需对照实际代码一次全量审阅（见 [DevLog §5 S3](DevLog.md)）
- [ ] `README.md` 需重写为 v0.1 定位页
- [ ] `docs/Build.md` 可选：把 [DevLog §2](DevLog.md) 的构建命令独立出来
- [ ] 样例场景缺一个"最小完整 demo"示范（当前 samples 都是演示单个功能）

---

## G. v0.3 ModSpec 已知缺口（2026-04-26 落地后）

> 主路径见 [ModSpec.md 附录 D](ModSpec.md)。本节只列"已知道、暂时没修"的边界。

### G1. cottage 仍在 `samples/content/`，未迁到 `mods/vanilla/`

附录 A 要求 `samples/cottage.toml` 重构为 `mods/vanilla/scenes/cottage.toml`。当前仍住在 `samples/content/scenes/cottage.toml`，是历史路径；不影响功能但和 ModSpec §1.1 "一个 mod 文件夹 = 一个完整游戏"心智不符。重构窗口：等 `mods/vanilla/` 体系定型一起迁。

### G2. MeshRenderer Material 字段仍 inline

`MeshRenderer` 反射 15 个字段（mesh + material + 5 张贴图路径）全部 inline 在组件上。附录 A 要求拆 Material sidecar；当前未拆，影响是 Material 不能被多个 MeshRenderer 共享、overlay 改 Material 必须按 component 改。

### G3. ark-validate CLI 缺位

§4.2 要求"检测对象 ID 在两次保存间漂移"为离线工具任务。当前在线校验已能在 `SceneDoc::Load` 把非法 persistent ID 推到 stderr，但 ID 漂移 / 重复 / Mod 内自洽这些规则需要 CLI 工具走全部 mod 文件，没做。

### ~~G4. §5 三种次要操作未做端到端实测~~ ✅ 2026-04-28 完结

`hellomod/scene.overlay.toml` 扩展为§5 四种操作全覆盖：deletions=1 (`core:torch_r`) / overrides=2 (`core:sun` / `core:torch_l`) / components_attached=1 (Light 加到 `core:metal_orb`) / additions=1 (`hellomod:welcome_sign` 发光方块。`ARK_AUTO_OVERLAY=1` 烟测日志确认 `applied: deletions=1 overrides=2 components_attached=1 additions=1`，stderr=0。

### G5. quicksave scope = 仅场景 + header

§6.1 完整 spec 要求 ECS systems 自定义状态（计时器、随机种子、scripting 状态等）也参与存档。当前 `HandleQuicksave` 只 dump `SaveHeader` + 整个 AScene；ECS 持久化未做。

### G6. §6.3 缺失 mod 时的玩家询问 UX 缺位

`SaveHeader::CheckCompatibility` 已能识别 `kMissingMod` / `kVersionMismatch` / `kSchemaMismatch`，`HandleQuickload` 在 incompatible 时打 WARN 并保留原场景。但 spec 所述"询问玩家继续 / 取消" 需要启动器侧 UI，跨进程，没做。

### G7. ScriptApi 仍 v2

`ARK_SCRIPT_API_VERSION = 2`。§15 ScriptApi v3（Camera 控制 / Component 反射访问 / quicksave 触发）未暴露到 C# 端。HelloMod 当前还是 v2 心智。

### G8. 新增 `.cpp` 必须 reconfigure

`engine/CMakeLists.txt` 用 `GLOB_RECURSE`。新增源文件后第一次 `nmake` 不会感知，链接时报 LNK2019。修复模式：`cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5` 跑一次再 build。规则上限是改 GLOB 为显式 list，但破坏性较大，暂留 known caveat。

### G9. PowerShell 终端 cwd 漂移 + .exe 锁

构建命令在持久 PowerShell 终端里执行时，cwd 可能停在 `build\samples`（上次 smoke 留下的），需显式 `Set-Location D:\Code\StarArkEngine`。另：上一次 smoke 的 `StarArkSamples.exe` 没退出会卡 `LNK1168`，构建前 `Get-Process StarArkSamples,StarArkGame | Stop-Process -Force`。
