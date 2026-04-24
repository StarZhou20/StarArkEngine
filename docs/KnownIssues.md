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

### B7. Light 匹配靠 `AObject::GetName()` 字符串相等

两个重名光源会互相覆盖；改名会让 JSON 里的对应条目失配。v0.1 **没有 GUID**。

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
