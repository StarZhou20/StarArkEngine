// main_samples.cpp — StarArkSamples entry point.
// Runs the engine-dev demo scenes. Not part of the shipping game.
//
// v0.3 ModSpec CLI (used by StarArkLauncher):
//   StarArkSamples.exe --game=<id> [--addon=<id>]... [--pipeline=forward|deferred]
//
// Legacy positional arg (dev-only convenience, kept for parity with old smokes):
//   StarArkSamples.exe                → FBXDemoScene  (Bistro, dev-only)
//   StarArkSamples.exe demo           → DemoScene     (PBR demo)
//   StarArkSamples.exe cottage        → CottageScene  (== --game=vanilla, kept as alias)
//   StarArkSamples.exe fbx            → FBXDemoScene  (Bistro)
#include "engine/core/EngineBase.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/debug/FileDebugListener.h"
#include "engine/platform/Paths.h"
#include "scenes/CottageScene.h"
#include "scenes/DemoScene.h"
#include "scenes/FBXDemoScene.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliArgs {
    std::string              game;
    std::vector<std::string> addons;
    std::string              pipeline;
    std::string              positional;
};

CliArgs ParseCli(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string_view s(argv[i] ? argv[i] : "");
        if (s.rfind("--game=", 0) == 0)            a.game = std::string(s.substr(7));
        else if (s.rfind("--addon=", 0) == 0)      a.addons.emplace_back(s.substr(8));
        else if (s.rfind("--pipeline=", 0) == 0)   a.pipeline = std::string(s.substr(11));
        else if (s.rfind("--", 0) == 0)            { /* unknown flag — ignore */ }
        else if (a.positional.empty())             a.positional = std::string(s);
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    ark::Paths::Init(argc > 0 ? argv[0] : nullptr);

    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener    fileListener;

    auto  cli    = ParseCli(argc, argv);
    auto& engine = ark::EngineBase::Get();

    if (!cli.pipeline.empty()) {
#ifdef _WIN32
        _putenv_s("ARK_PIPELINE", cli.pipeline.c_str());
#else
        setenv("ARK_PIPELINE", cli.pipeline.c_str(), 1);
#endif
    }

    if (!cli.game.empty()) {
        const std::string title = "StarArk — " + cli.game;
        // Engine-dev game mods backed by hardcoded C++ scenes (no scenes/main.toml).
        // Listed in samples/mods/<id>/mod.toml so the launcher can discover them.
        if (cli.game == "bistro") {
            ARK_LOG_INFO("Boot",
                "Selected scene = game mod 'bistro' (FBXDemoScene)");
            engine.Run<FBXDemoScene>(2560, 1440, title);
            return 0;
        }
        // Default game-mod path: data-driven CottageScene loads mods/<id>/scenes/main.toml.
        CottageScene::SetActiveGameMod(cli.game);
        ARK_LOG_INFO("Boot",
            std::string("Selected scene = game mod '") + cli.game + "'");
        engine.Run<CottageScene>(2560, 1440, title);
        return 0;
    }

    if (cli.positional == "cottage") {
        CottageScene::SetActiveGameMod("vanilla");
        ARK_LOG_INFO("Boot", "Selected scene = Cottage (alias of --game=vanilla)");
        engine.Run<CottageScene>(2560, 1440, "StarArk Samples — Cottage (v0.1)");
    } else if (cli.positional == "demo") {
        ARK_LOG_INFO("Boot", "Selected scene = PBR Demo");
        engine.Run<DemoScene>(2560, 1440, "StarArk Samples — PBR Demo");
    } else {
        ARK_LOG_INFO("Boot", "Selected scene = Bistro (FBX, dev default)");
        engine.Run<FBXDemoScene>(2560, 1440, "StarArk Samples — Bistro (FBX)");
    }
    return 0;
}
