// main.cpp — StarArkGame entry point (empty template).
//
// This is the minimal game shell. When distributing the engine as SDK,
// this file is what users copy and extend with their own scenes.
//
// Replace EmptyScene with your first scene, e.g.:
//   #include "scenes/MainMenu.h"
//   ark::EngineBase::Get().Run<MainMenu>(1280, 720, "My Game");

#include "engine/core/EngineBase.h"
#include "engine/core/AScene.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/debug/FileDebugListener.h"
#include "engine/platform/Paths.h"

// Minimal placeholder scene: empty, just proves the engine boots.
class EmptyScene : public ark::AScene {
public:
    void OnLoad() override { SetSceneName("EmptyScene"); }
    void OnUnload() override {}
};

int main(int argc, char** argv) {
    ark::Paths::Init(argc > 0 ? argv[0] : nullptr);

    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener    fileListener;

    ark::EngineBase::Get().Run<EmptyScene>(1280, 720, "StarArk Game");
    return 0;
}
