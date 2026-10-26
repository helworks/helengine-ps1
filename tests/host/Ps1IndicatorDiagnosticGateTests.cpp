#include "platform/ps1/rendering/Ps1IndicatorDiagnosticGate.hpp"
#include <cassert>
#include <cstdio>

int main() {
    helengine::ps1::Ps1IndicatorDiagnosticGate gate;
    int commands = 0, otherCommands = 0, texture = 0, otherTexture = 0;
    assert(gate.Observe(&commands, 1, &texture, 7));
    for (unsigned frame = 0; frame < 600; ++frame)
        assert(!gate.Observe(&commands, 1, &texture, 7));
    assert(gate.Observe(&commands, 2, &texture, 7));
    assert(!gate.Observe(&commands, 2, &texture, 7));
    assert(gate.Observe(&commands, 2, &otherTexture, 7));
    assert(gate.Observe(&otherCommands, 2, &otherTexture, 7));
    // A newly uploaded texture can reuse exactly the old native address.
    assert(gate.Observe(&otherCommands, 2, &otherTexture, 8));
    assert(!gate.Observe(&otherCommands, 2, &otherTexture, 8));
    // No commands means borrowed identities have left the indicator lifecycle.
    assert(!gate.Observe(nullptr, 2, &otherTexture, 8));
    assert(gate.Observe(&otherCommands, 2, &otherTexture, 8));
    assert(!gate.Observe(&otherCommands, 2, nullptr, 8));
    assert(gate.Observe(&otherCommands, 2, &otherTexture, 8));
    gate.Reset();
    assert(gate.Observe(&otherCommands, 2, &otherTexture, 8));
    assert(!gate.Observe(&otherCommands, 0, &otherTexture, 8));
    assert(gate.Observe(&otherCommands, 1, &otherTexture, 8));
    std::puts("PASS PS1 indicator diagnostic one-shot lifecycle and native-address reuse");
}
