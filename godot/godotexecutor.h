#ifndef GODOTEXECUTOR_H
#define GODOTEXECUTOR_H

#include <string>

namespace GodotExecutor
{
    void Initialize();
    bool IsInitialized();
    bool ExecuteScript(const std::string& script, std::string* error = nullptr);
}

#endif /* GODOTEXECUTOR_H */
