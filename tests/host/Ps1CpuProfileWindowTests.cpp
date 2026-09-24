#include "platform/ps1/profiling/Ps1CpuProfileWindow.hpp"

#include <cstdint>
#include <cstring>

int main() {
    using helengine::ps1::Ps1CpuProfileWindow;

    char milliseconds[16]{};
    if (!Ps1CpuProfileWindow::TryFormatMilliseconds(1638400u, 600u, milliseconds)) return 1;
    if (std::strcmp(milliseconds, "2.730") != 0) return 2;

    Ps1CpuProfileWindow complete;
    for (std::uint64_t frame = 1; frame <= 600; ++frame) {
        complete.BeginFrame(frame, static_cast<std::uint32_t>(frame), 42u);
        complete.AddStage(1u, 64u);
        complete.AddComponent(static_cast<std::uintptr_t>(0x1001u), 3u, 0xAAu, 7u, 64u);
        complete.AddComponent(static_cast<std::uintptr_t>(0x1002u), 4u, 0xAAu, 7u, 64u);
        complete.EndFrame(frame, static_cast<std::uint32_t>(frame));
    }
    if (!complete.IsReportReady()) return 3;
    const Ps1CpuProfileWindow::ReportView& report = complete.GetReport();
    if (report.FrameCount != 600u || report.FirstSequence != 1u || report.LastSequence != 600u) return 4;
    if (report.ComponentCount != 2u || report.StageCount != 1u) return 5;
    if (report.Components[0].Identity == report.Components[1].Identity) return 6;
    if (!Ps1CpuProfileWindow::TryFormatMilliseconds(report.Components[0].TotalMicroseconds,
                                                     report.Components[0].Calls,
                                                     milliseconds)) return 7;
    if (std::strcmp(milliseconds, "0.064") != 0) return 8;

    // A report remains stable until the host explicitly consumes it.
    complete.BeginFrame(601u, 601u, 42u);
    complete.AddStage(1u, 64u);
    complete.EndFrame(601u, 601u);
    if (!complete.IsReportReady() || complete.GetReport().FrameCount != 600u) return 9;
    complete.ClearReport();

    // A changed component order resets the window instead of mixing identities.
    Ps1CpuProfileWindow changed;
    for (std::uint64_t frame = 1; frame <= 600; ++frame) {
        changed.BeginFrame(frame, static_cast<std::uint32_t>(frame), 42u);
        if (frame == 300u) {
            changed.AddComponent(static_cast<std::uintptr_t>(0x1002u), 4u, 0xAAu, 7u, 64u);
            changed.AddComponent(static_cast<std::uintptr_t>(0x1001u), 3u, 0xAAu, 7u, 64u);
        } else {
            changed.AddComponent(static_cast<std::uintptr_t>(0x1001u), 3u, 0xAAu, 7u, 64u);
            changed.AddComponent(static_cast<std::uintptr_t>(0x1002u), 4u, 0xAAu, 7u, 64u);
        }
        changed.EndFrame(frame, static_cast<std::uint32_t>(frame));
    }
    if (changed.ResetCount() == 0u || changed.IsReportReady()) return 10;

    // Capacity overflow drops the extra row without touching the fixed storage guards.
    Ps1CpuProfileWindow overflow;
    overflow.BeginFrame(1u, 1u, 42u);
    for (std::uintptr_t identity = 0x2000u; identity < 0x2081u; ++identity) {
        overflow.AddComponent(identity, static_cast<std::uint32_t>(identity), 0xBBu, 8u, 64u);
    }
    overflow.EndFrame(1u, 1u);
    if (overflow.DroppedCount() != 1u || overflow.GetReport().ComponentCount != 128u) return 11;

    // A clock rollback discards the window rather than wrapping uint64_t.
    Ps1CpuProfileWindow clockFault;
    clockFault.BeginFrame(1u, 1u, 42u);
    clockFault.ClockFault();
    clockFault.EndFrame(1u, 1u);
    if (clockFault.ResetCount() == 0u || clockFault.IsReportReady()) return 12;

    return 0;
}
