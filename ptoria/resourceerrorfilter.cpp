#include <ptoria/resourceerrorfilter.h>

#include <hooking/hookmanager.h>
#include <unity/unity.h>

#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

namespace
{
    using PrintErrFn = void (*)(UnityArray<UnityObject*>*);

    std::mutex g_seenResourceErrorsLock;
    std::unordered_set<std::string> g_seenResourceErrors;

    bool IsResourceLoadFailure(const std::string& message)
    {
        return message.find("Failed to load resource (Type: ") != std::string::npos &&
               message.find("Response status code does not indicate success:") != std::string::npos;
    }

    std::string BuildMessage(UnityArray<UnityObject*>* args)
    {
        if (args == nullptr || args->max_length == 0)
        {
            return {};
        }

        std::string message;
        auto** values = reinterpret_cast<UnityObject**>(args->GetData());
        for (std::uintptr_t i = 0; i < args->max_length; ++i)
        {
            UnityObject* value = values[i];
            if (value == nullptr)
            {
                if (!message.empty())
                {
                    message += " ";
                }
                message += "null";
                continue;
            }

            UnityString* valueString = value->ToString();
            if (valueString == nullptr)
            {
                continue;
            }

            if (!message.empty())
            {
                message += " ";
            }
            message += valueString->ToString();
        }

        return message;
    }

    bool ShouldSuppressResourceError(const std::string& message)
    {
        if (!IsResourceLoadFailure(message))
        {
            return false;
        }

        std::lock_guard lock(g_seenResourceErrorsLock);
        return !g_seenResourceErrors.insert(message).second;
    }

    void PrintErrHook(UnityArray<UnityObject*>* args)
    {
        const std::string message = BuildMessage(args);
        if (ShouldSuppressResourceError(message))
        {
            spdlog::debug("Suppressed duplicate Polytoria resource load error: {}", message);
            return;
        }

        HookManager::Call(PrintErrHook, args);
    }
}

void ResourceErrorFilter::InstallHooks()
{
    UnityAssembly* assembly = Unity::GetAssembly<Unity::AssemblyCSharp>();
    if (assembly == nullptr)
    {
        spdlog::warn("Resource error filter skipped: Assembly-CSharp.dll was not found");
        return;
    }

    UnityClass* ptClass = assembly->Get("PT", "Polytoria.Shared");
    if (ptClass == nullptr)
    {
        spdlog::warn("Resource error filter skipped: Polytoria.Shared.PT was not found");
        return;
    }

    UnityMethod* printErrMethod = ptClass->Get<UnityMethod>("PrintErr", { "System.Object[]" });
    if (printErrMethod == nullptr)
    {
        printErrMethod = ptClass->Get<UnityMethod>("PrintErr");
    }

    if (printErrMethod == nullptr)
    {
        spdlog::warn("Resource error filter skipped: Polytoria.Shared.PT.PrintErr was not found");
        return;
    }

    PrintErrFn printErr = printErrMethod->Cast<void, UnityArray<UnityObject*>*>();
    if (!HookManager::Install(printErr, PrintErrHook))
    {
        spdlog::warn("Resource error filter skipped: failed to hook Polytoria.Shared.PT.PrintErr");
        return;
    }

    spdlog::info("Installed duplicate Polytoria resource load error filter");
}
