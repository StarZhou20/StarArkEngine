// main_samples.cpp — StarArkSamples entry point.
// Runs the engine-dev demo scenes. Not part of the shipping game.
//
// Usage:
//   StarArkSamples.exe            → FBXDemoScene (Bistro, default)
//   StarArkSamples.exe demo       → DemoScene (PBR spheres + model)
//   StarArkSamples.exe cottage    → CottageScene (v0.1 minimal self-contained demo)
//   StarArkSamples.exe fbx        → FBXDemoScene (Bistro)
#include "engine/core/EngineBase.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/debug/FileDebugListener.h"
#include "engine/platform/Paths.h"
#include "scenes/CottageScene.h"
#include "scenes/DemoScene.h"
#include "scenes/FBXDemoScene.h"

#include <cstring>

int main(int argc, char** argv) {
    ark::Paths::Init(argc > 0 ? argv[0] : nullptr);

    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener    fileListener;

    const char* pick = (argc > 1) ? argv[1] : "";
    auto& engine = ark::EngineBase::Get();

    if (std::strcmp(pick, "cottage") == 0) {
        engine.Run<CottageScene>(2560, 1440, "StarArk Samples — Cottage (v0.1)");
    } else if (std::strcmp(pick, "demo") == 0) {
        engine.Run<DemoScene>(2560, 1440, "StarArk Samples — PBR Demo");
    } else {
        // Default: Bistro FBX scene
        engine.Run<FBXDemoScene>(2560, 1440, "StarArk Samples — Bistro (FBX)");
    }
    return 0;
}
