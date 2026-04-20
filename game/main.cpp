// main.cpp — StarArk Engine Entry Point
#include "engine/core/EngineBase.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/debug/FileDebugListener.h"
#include "scenes/FBXDemoScene.h"

int main() {
    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener fileListener;

    ark::EngineBase::Get().Run<FBXDemoScene>(1280, 720, "StarArk Engine — FBX Demo");
    return 0;
}
