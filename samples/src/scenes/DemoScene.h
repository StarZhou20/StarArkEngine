#pragma once

#include "engine/core/AScene.h"

class DemoScene : public ark::AScene {
public:
    void OnLoad() override;
    void OnUnload() override;
};
