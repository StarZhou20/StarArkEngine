// CottageScene.h — v0.1-renderer 最小完整样例 / Minimal full demo scene.
//
// 设计目的 (Purpose):
//   证明 StarArkEngine v0.1 的文档是自洽的——本文件**只用 engine 公共 API**，
//   不依赖 samples/ 下的任何帮助类。一个陌生的 AI 或开发者读完 docs/Capabilities.md
//   应该能写出**本文件级别**的 demo。
//
// 内容 (Contents):
//   - 一块棋盘地面（Mesh::CreatePlane + PBR）
//   - 一个"小屋"立方体（Mesh::CreateCube + PBR 石材参数）
//   - 一个"太阳"方向光（投射阴影）
//   - 两个"火把"点光（暖色 1/r² 衰减）
//   - OrbitCamera（右键旋转 / 滚轮缩放 / 中键平移）
//   - ESC 退出
//   - SceneSerializer 热重载 lighting.json
//
// 启动 (Launch): StarArkSamples.exe cottage
#pragma once

#include "engine/core/AScene.h"

class CottageScene : public ark::AScene {
public:
    void OnLoad() override;
    void Tick(float /*dt*/) override {}
    void OnUnload() override {}
};
