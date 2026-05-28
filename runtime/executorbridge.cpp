#include <runtime/executorbridge.h>

#include <godot/godotexecutor.h>
#include <ptoria/scriptinstance.h>
#include <ptoria/scriptservice.h>

namespace ExecutorBridge
{
    namespace
    {
        Runtime::Engine g_engine = Runtime::Engine::Unknown;
    }

    void SetEngine(Runtime::Engine engine)
    {
        g_engine = engine;
    }

    Runtime::Engine GetEngine()
    {
        return g_engine;
    }

    bool ExecuteScript(const std::string& script, std::string* error)
    {
        switch (g_engine)
        {
        case Runtime::Engine::LegacyUnity:
            ScriptService::RunScript<ScriptInstance>(script);
            return true;
        case Runtime::Engine::Godot:
            return GodotExecutor::ExecuteScript(script, error);
        case Runtime::Engine::Unknown:
        default:
            if (error)
            {
                *error = "No supported Polytoria runtime has been initialized";
            }
            return false;
        }
    }
}
