# StarArkEngine Tools

独立工具集合，**与引擎代码零耦合**——仅通过文件系统（目前是 `content/*.json`）与运行中的引擎通讯。

每个子目录一个工具，互不依赖，可独立运行/删除。

| 工具 | 技术栈 | 用途 |
|------|------|------|
| [`LightingTuner/`](LightingTuner/README.md) | C# / WPF (.NET 10) | 实时编辑 `lighting.json`：曝光、Bloom、IBL、Shadow、所有光源参数。单文件 exe ≈ 180 KB |

## 约定

- 不链接 StarArkEngine 的头文件 / 库
- 不引入 C++ 依赖
- 所有工具都以**文件**为输入/输出，引擎端已有 mtime 热重载
- 优先产出单文件 exe，便于发行
