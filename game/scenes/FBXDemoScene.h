#pragma once

#include "engine/core/AScene.h"

class FBXDemoScene : public ark::AScene {
public:
    void OnLoad() override;
    void OnUnload() override;
};
