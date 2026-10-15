#include "platform/ps1/rendering/Ps1TextLayout.hpp"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); ++failures; }
}

bool Lookup(char ch, helengine::ps1::Ps1TextGlyph& glyph) {
    if (ch != 'N' && ch != 'Z' && ch != 'P') return false;
    glyph = {4.0f, 6.0f, 1.0f, 5.0f};
    glyph.bearingX = ch == 'N' ? -2.0f : ch == 'P' ? 2.0f : 0.0f;
    glyph.sourceValid = true;
    return true;
}

void CheckLayout(helengine::ps1::Ps1TextAlignment alignment, float n, float z, float p) {
    float x[3]{}, y[3]{};
    unsigned count = 0;
    const auto emit = [&](char, const helengine::ps1::Ps1TextGlyph&, float px, float py) {
        if (count == 3) return false;
        x[count] = px; y[count++] = py; return true;
    };
    Check(helengine::ps1::LayoutPs1Text("NZP", 3, 10, 20, 16, 2, 2, 40,
                                      alignment, Lookup, emit), "real layout succeeds");
    Check(count == 3, "negative, zero and positive bearing glyphs all emitted");
    Check(x[0] == n && x[1] == z && x[2] == p,
          "glyph ink origins and center/right alignment honor cooked horizontal bearings");
    Check(y[0] == 22 && y[1] == 22 && y[2] == 22, "vertical bearing remains unchanged");
}

void TestLayout() {
    using helengine::ps1::Ps1TextAlignment;
    CheckLayout(Ps1TextAlignment::Left, 6, 20, 34);
    CheckLayout(Ps1TextAlignment::Center, 10, 24, 38);
    CheckLayout(Ps1TextAlignment::Right, 14, 28, 42);
}

void TestCachedCharacterReplacement() {
    using namespace helengine::ps1;
    Ps1TextGlyphCache cache;
    int font = 0;
    Ps1TextGlyph negative{}, zero{}, positive{};
    Check(cache.Lookup('N', &font, negative, Lookup), "real cache loads negative bearing");
    Check(cache.Lookup('Z', &font, zero, Lookup), "real cache loads zero bearing");
    Check(cache.Lookup('P', &font, positive, Lookup), "real cache loads positive bearing");
    Check(negative.bearingX == -2 && zero.bearingX == 0 && positive.bearingX == 2,
          "glyph metric cache retains horizontal bearings");
    Check(!CanReusePs1TextGlyphGeometry(negative, positive),
          "UV-only replacement rejects changed horizontal bearing");
    Check(!CanReusePs1TextGlyphGeometry(negative, zero),
          "UV-only replacement rejects bearing becoming zero");
    auto otherAtlasLocation = negative;
    otherAtlasLocation.sourceX = 41; otherAtlasLocation.sourceY = 29;
    Check(CanReusePs1TextGlyphGeometry(negative, otherAtlasLocation),
          "UV-only replacement remains allowed for equal geometry at another atlas position");
    otherAtlasLocation.sourceValid = false;
    Check(!CanReusePs1TextGlyphGeometry(negative, otherAtlasLocation),
          "invalid replacement source remains rejected");
    cache.Invalidate();
    auto changedFont = [](char ch, Ps1TextGlyph& glyph) {
        const bool found = Lookup(ch, glyph); glyph.bearingX = 3; return found;
    };
    Check(cache.Lookup('N', &font, negative, changedFont) && negative.bearingX == 3,
          "font cache invalidation refreshes horizontal bearing");
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--cache-only") == 0) TestCachedCharacterReplacement();
    else TestLayout();
    if (failures != 0) return 1;
    std::puts("PASS PS1 horizontal glyph bearings");
    return 0;
}
