#pragma once
#include <cstdint>

namespace helengine::ps1 {

/// <summary>Bounds the temporary badge investigation to one trace per borrowed layout/texture identity.</summary>
class Ps1IndicatorDiagnosticGate {
public:
    /// <summary>Reports a changed command version or texture upload lifetime without retaining or dereferencing either resource.</summary>
    bool Observe(const void* commands, std::int32_t version, const void* texture, std::uint32_t uploads) {
        if (commands == nullptr || texture == nullptr || version <= 0) {
            Reset();
            return false;
        }
        if (Commands == commands && Version == version && Texture == texture && Uploads == uploads) return false;
        Commands = commands;
        Version = version;
        Texture = texture;
        Uploads = uploads;
        return true;
    }

    /// <summary>Forgets borrowed addresses whenever the indicator has no live command payload.</summary>
    void Reset() { Commands = Texture = nullptr; Version = 0; Uploads = 0; }

private:
    /// <summary>Last borrowed command-list address; never dereferenced after observation.</summary>
    const void* Commands = nullptr;
    /// <summary>Last borrowed atlas address, distinguished from a later allocation by the renderer upload counter.</summary>
    const void* Texture = nullptr;
    /// <summary>Existing command-list version that advances when the Core rebuilds its cached badge layout.</summary>
    std::int32_t Version = 0;
    /// <summary>Existing monotonic renderer upload count, preventing suppression after native-address reuse.</summary>
    std::uint32_t Uploads = 0;
};

} // namespace helengine::ps1
