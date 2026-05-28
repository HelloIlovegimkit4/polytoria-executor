#ifndef RUNTIME_H
#define RUNTIME_H

#include <string>

namespace Runtime
{
    enum class Engine
    {
        Unknown,
        LegacyUnity,
        Godot
    };

    Engine DetectEngine();
    const char* ToString(Engine engine);
}

#endif /* RUNTIME_H */
