#include <godot/godotexecutor.h>

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>

namespace GodotExecutor
{
    namespace
    {
        std::mutex g_stateLock;
        bool g_initialized = false;
        std::filesystem::path g_spoolDirectory;

        std::filesystem::path GetSpoolDirectory()
        {
            wchar_t localAppData[MAX_PATH]{};
            DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
            std::filesystem::path base = written != 0 ? std::filesystem::path(localAppData)
                                                      : std::filesystem::temp_directory_path();
            return base / L"PolyHack" / L"GodotSpool";
        }

        std::filesystem::path NextScriptPath()
        {
            SYSTEMTIME time{};
            GetSystemTime(&time);

            wchar_t fileName[128]{};
            swprintf_s(fileName,
                       L"script-%04u%02u%02u-%02u%02u%02u-%03u-%lu.luau",
                       time.wYear,
                       time.wMonth,
                       time.wDay,
                       time.wHour,
                       time.wMinute,
                       time.wSecond,
                       time.wMilliseconds,
                       GetCurrentThreadId());

            return g_spoolDirectory / fileName;
        }
    }

    void Initialize()
    {
        std::lock_guard lock(g_stateLock);
        if (g_initialized)
        {
            return;
        }

        g_spoolDirectory = GetSpoolDirectory();
        std::error_code ec;
        std::filesystem::create_directories(g_spoolDirectory, ec);
        if (ec)
        {
            spdlog::error("[GodotExecutor] Failed to create script spool directory {}: {}",
                          g_spoolDirectory.string(),
                          ec.message());
            return;
        }

        g_initialized = true;
        spdlog::info("[GodotExecutor] Initialized Godot runtime bridge; script spool: {}", g_spoolDirectory.string());
        spdlog::warn("[GodotExecutor] Unity IL2CPP hooks are disabled for Godot Polytoria. Scripts are spooled until a managed Godot/Luau bridge is available.");
    }

    bool IsInitialized()
    {
        std::lock_guard lock(g_stateLock);
        return g_initialized;
    }

    bool ExecuteScript(const std::string& script, std::string* error)
    {
        std::lock_guard lock(g_stateLock);
        if (!g_initialized)
        {
            if (error)
            {
                *error = "Godot executor bridge is not initialized";
            }
            return false;
        }

        std::filesystem::path scriptPath = NextScriptPath();
        std::ofstream stream(scriptPath, std::ios::binary);
        if (!stream)
        {
            if (error)
            {
                *error = "Failed to open script spool file: " + scriptPath.string();
            }
            return false;
        }

        stream.write(script.data(), static_cast<std::streamsize>(script.size()));
        stream.close();

        spdlog::warn("[GodotExecutor] Script accepted but not executed; wrote {} bytes to {}. Native Unity hooks cannot drive Godot/Luau.",
                     script.size(),
                     scriptPath.string());
        return true;
    }
}
