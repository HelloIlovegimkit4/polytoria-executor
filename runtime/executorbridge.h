#ifndef EXECUTORBRIDGE_H
#define EXECUTORBRIDGE_H

#include <runtime/runtime.h>
#include <string>

namespace ExecutorBridge
{
    void SetEngine(Runtime::Engine engine);
    Runtime::Engine GetEngine();
    bool ExecuteScript(const std::string& script, std::string* error = nullptr);
}

#endif /* EXECUTORBRIDGE_H */
