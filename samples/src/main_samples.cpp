// main_samples.cpp — StarArkSamples entry point.
// Runs the engine-dev demo scenes. Not part of the shipping game.
#include "engine/core/EngineBase.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/debug/FileDebugListener.h"
#include "engine/platform/Paths.h"
#include "scenes/DemoScene.h"

int main(int argc, char** argv) {
    ark::Paths::Init(argc > 0 ? argv[0] : nullptr);

    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener    fileListener;

    ark::EngineBase::Get().Run<DemoScene>(1280, 720, "StarArk Samples — PBR Demo");
    return 0;
}
