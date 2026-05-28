#include <ptoria/instance.h>
#include <nasec/assert.h>

#include <chrono>
#include <core/core.h>
#include <godot/godotexecutor.h>
#include <mirror/hooks.h>
#include <ptoria/game.h>
#include <ptoria/networkevent.h>
#include <ptoria/resourceerrorfilter.h>
#include <ptoria/scriptinstance.h>
#include <ptoria/scriptservice.h>
#include <runtime/executorbridge.h>
#include <runtime/runtime.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <thread>
#include <ui/ui.h>
#include <cheat/pipe.h>

namespace
{
    Runtime::Engine WaitForRuntime()
    {
        for (int i = 0; i < 120; ++i)
        {
            Runtime::Engine engine = Runtime::DetectEngine();
            if (engine != Runtime::Engine::Unknown)
            {
                return engine;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        return Runtime::Engine::Unknown;
    }

    int StartGodotRuntime()
    {
        ExecutorBridge::SetEngine(Runtime::Engine::Godot);
        GodotExecutor::Initialize();

        StartPipeServer();

        spdlog::info("Godot Polytoria detected. Legacy Unity hooks, IL2CPP reflection, Mirror hooks, and DirectX Unity UI were not installed.");
        spdlog::warn("Use the WPF launcher to send scripts; they will be spooled until the managed Godot/Luau bridge is implemented.");
        return 0;
    }

    int StartLegacyUnityRuntime()
    {
        Unity::Init();
        Unity::ThreadAttach();

        UnityAssembly* assembly = Unity::GetAssembly<Unity::AssemblyCSharp>();

        // We gotta wait for game to be started !
        void* game = Unity::GetStaticFieldValue<void*, "singleton">(StaticClass<Game>());
        while (!game) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            game = Unity::GetStaticFieldValue<void*, "singleton">(StaticClass<Game>());
        }

        ExecutorBridge::SetEngine(Runtime::Engine::LegacyUnity);
        UI::state = UI::UiState::Ready;

        spdlog::info("Assembly Name: {}", assembly->name);
        UnityClass* instanceClass = StaticClass<Instance>();
        nasec::Assert(instanceClass != nullptr, "Failed to get Instance class");

        UnityClass* gameClass = StaticClass<Game>();
        nasec::Assert(gameClass != nullptr, "Failed to get Game class");

        spdlog::info("Instance Class Name: {}", instanceClass->name);
        spdlog::info("Game Class Name: {}", gameClass->name);

        Game* gameInstance = Game::GetSingleton();
        nasec::Assert(gameInstance != nullptr, "Failed to get Game singleton instance");

        spdlog::info("Game Instance Address: 0x{:016X}", reinterpret_cast<uintptr_t>(gameInstance));

        spdlog::info("Game Instance Name: {}", gameInstance->Name()->ToString());
        spdlog::info("Game Instance Full Name: {}", gameInstance->FullName()->ToString());
        for (auto& child: gameInstance->Children()->ToVector()) {
            spdlog::info("Child Instance Name: {}", child->Name()->ToString());
        }

        spdlog::info("ScriptInstance Class Name: {}", StaticClass<ScriptInstance>()->name);
        ResourceErrorFilter::InstallHooks();
        ScriptService::InstallHooks();
        ScriptService* scriptService = ScriptService::GetInstance();
        nasec::Assert(scriptService != nullptr, "Failed to get ScriptService instance");
        //ScriptService::RunScript<ScriptInstance>(R"(InvokeServerHook(game["Hidden"]["DraggerPlace"], function(msg) print(msg) end))");

        mirror::InstallHooks();

        // Start the named pipe server for external script execution
        StartPipeServer();

        UI::Setup();
        return 0;
    }
}

int main_thread()
{
    OpenConsole();

    Runtime::Engine engine = WaitForRuntime();
    spdlog::info("Detected Polytoria runtime: {}", Runtime::ToString(engine));

    switch (engine)
    {
    case Runtime::Engine::Godot:
        return StartGodotRuntime();
    case Runtime::Engine::LegacyUnity:
        return StartLegacyUnityRuntime();
    case Runtime::Engine::Unknown:
    default:
        spdlog::error("No supported Polytoria runtime was detected. Refusing to install legacy Unity hooks blindly.");
        ExecutorBridge::SetEngine(Runtime::Engine::Unknown);
        StartPipeServer();
        return 1;
    }
}
