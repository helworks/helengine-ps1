#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

struct Ps1FrameOverlaySample {
    std::uint32_t frameCount;
    std::uint64_t elapsedMicroseconds;
    std::uint64_t updateTotalMicroseconds;
    std::uint64_t renderMicroseconds;
};

struct Ps1FrameOverlayText {
    char update[48]{};
    char render[48]{};
};

namespace frame_overlay_detail {

class Writer {
public:
    Writer(char* output, std::size_t capacity) : Cursor(output), End(output + capacity) {}

    bool Append(const char* text) {
        while (*text != '\0') {
            if (Cursor + 1 >= End) return false;
            *Cursor++ = *text++;
        }
        return true;
    }

    bool AppendUnsigned(std::uint64_t value) {
        char digits[20]{};
        unsigned count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10u);
            value /= 10u;
        } while (value != 0);
        while (count != 0) {
            if (Cursor + 1 >= End) return false;
            *Cursor++ = digits[--count];
        }
        return true;
    }

    bool AppendTenths(std::uint64_t value) {
        if (!AppendUnsigned(value / 10u)) return false;
        if (Cursor + 3 > End) return false;
        *Cursor++ = '.';
        *Cursor++ = static_cast<char>('0' + value % 10u);
        return true;
    }

    bool Finish() {
        if (Cursor >= End) return false;
        *Cursor = '\0';
        return true;
    }

private:
    char* Cursor;
    char* End;
};

}  // namespace frame_overlay_detail

/// Formats platform frame cadence and measured update/render cost with one decimal place.
inline bool TryFormatFrameOverlayText(Ps1FrameOverlaySample sample, Ps1FrameOverlayText& text) {
    if (sample.frameCount == 0 || sample.elapsedMicroseconds == 0) return false;
    const std::uint64_t fpsTenths =
        (static_cast<std::uint64_t>(sample.frameCount) * 10000000u + sample.elapsedMicroseconds / 2u) /
        sample.elapsedMicroseconds;
    const std::uint64_t updateTenths =
        (sample.updateTotalMicroseconds + static_cast<std::uint64_t>(sample.frameCount) * 50u) /
        (static_cast<std::uint64_t>(sample.frameCount) * 100u);
    const std::uint64_t renderTenths = (sample.renderMicroseconds + 50u) / 100u;
    frame_overlay_detail::Writer update(text.update, sizeof(text.update));
    frame_overlay_detail::Writer render(text.render, sizeof(text.render));
    return update.Append("Update FPS: ") && update.AppendTenths(fpsTenths) &&
           update.Append(" (") && update.AppendTenths(updateTenths) && update.Append(" ms)") && update.Finish() &&
           render.Append("Render FPS: ") && render.AppendTenths(fpsTenths) &&
           render.Append(" (") && render.AppendTenths(renderTenths) && render.Append(" ms)") && render.Finish();
}

}  // namespace helengine::ps1