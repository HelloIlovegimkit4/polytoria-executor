#include <runtime/runtime.h>

#include <Windows.h>
#include <string>

namespace Runtime
{
    namespace
    {
        bool HasModule(const wchar_t* moduleName)
        {
            return GetModuleHandleW(moduleName) != nullptr;
        }
    }

    Engine DetectEngine()
    {
        if (HasModule(L"GameAssembly.dll"))
        {
            return Engine::LegacyUnity;
        }

        if (HasModule(L"GodotSharp.dll") ||
            HasModule(L"GodotSharp.Core.dll") ||
            HasModule(L"coreclr.dll") ||
            HasModule(L"hostfxr.dll"))
        {
            return Engine::Godot;
        }

        wchar_t processPath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, processPath, MAX_PATH) != 0)
        {
            std::wstring path = processPath;
            if (path.find(L"Polytoria") != std::wstring::npos)
            {
                return Engine::Godot;
            }
        }

        return Engine::Unknown;
    }

    const char* ToString(Engine engine)
    {
        switch (engine)
        {
        case Engine::LegacyUnity:
            return "Legacy Unity";
        case Engine::Godot:
            return "Godot";
        case Engine::Unknown:
        default:
            return "Unknown";
        }
    }
}
