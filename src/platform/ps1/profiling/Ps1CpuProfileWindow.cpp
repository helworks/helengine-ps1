#include "platform/ps1/profiling/Ps1CpuProfileWindow.hpp"

#include <cstring>
#include <limits>

namespace helengine::ps1 {

bool Ps1CpuProfileWindow::KeysEqual(const ComponentKey& left, const ComponentKey& right) {
    return left.Identity == right.Identity && left.Index == right.Index
        && left.TypeHash == right.TypeHash && left.OwnerSceneEntityId == right.OwnerSceneEntityId;
}

bool Ps1CpuProfileWindow::AddDuration(std::uint64_t& total, std::uint32_t& calls,
                                      std::uint64_t elapsed) {
    if (elapsed > std::numeric_limits<std::uint64_t>::max() - total
        || calls == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    total += elapsed;
    ++calls;
    return true;
}

void Ps1CpuProfileWindow::ResetStorage() {
    StageCount = 0;
    ComponentCount = 0;
    BaselineComponentCount = 0;
    CurrentComponentCount = 0;
    FrameCount = 0;
    FrameActive = false;
    FrameDiscarded = false;
    HaveBaseline = false;
    HavePreviousSequence = false;
    ReportReady = false;
    CurrentSequence = 0;
    PreviousSequence = 0;
    FirstSequence = 0;
    LastSequenceValue = 0;
    CurrentVblank = 0;
    FirstVblank = 0;
    LastVblankValue = 0;
    SceneId = 0;
    for (StageRow& row : Stages) {
        row = StageRow{};
    }
    for (ComponentRow& row : Components) {
        row = ComponentRow{};
    }
    for (ComponentKey& key : BaselineComponents) {
        key = ComponentKey{};
    }
    for (ComponentKey& key : CurrentComponents) {
        key = ComponentKey{};
    }
}

void Ps1CpuProfileWindow::DiscardWindow(ResetReason reason) {
    ++Resets;
    LastResetReason = reason;
    ResetStorage();
}

void Ps1CpuProfileWindow::Reset(ResetReason reason) {
    DiscardWindow(reason);
}

void Ps1CpuProfileWindow::ClockFault() {
    ++ClockFaults;
    DiscardWindow(ResetReason::ClockFault);
}

void Ps1CpuProfileWindow::BeginFrame(std::uint64_t sequence, std::uint32_t vblank,
                                     std::uint32_t sceneId) {
    if (ReportReady) {
        ++Dropped;
        return;
    }
    if (FrameActive) {
        DiscardWindow(ResetReason::InvalidFrame);
    }
    if (HavePreviousSequence && sequence != PreviousSequence + 1u) {
        DiscardWindow(ResetReason::SequenceGap);
    }
    if (FrameCount != 0u && sceneId != SceneId) {
        DiscardWindow(ResetReason::SceneChanged);
    }

    FrameActive = true;
    FrameDiscarded = false;
    CurrentSequence = sequence;
    CurrentVblank = vblank;
    SceneId = sceneId;
    CurrentComponentCount = 0;
    for (ComponentKey& key : CurrentComponents) {
        key = ComponentKey{};
    }
}

void Ps1CpuProfileWindow::AddStage(std::uint32_t stageId, std::uint64_t elapsedMicroseconds) {
    if (!IsFrameUsable()) {
        return;
    }
    for (std::size_t i = 0; i < StageCount; ++i) {
        if (Stages[i].StageId == stageId) {
            if (!AddDuration(Stages[i].TotalMicroseconds, Stages[i].Calls, elapsedMicroseconds)) {
                DiscardWindow(ResetReason::Overflow);
            }
            return;
        }
    }
    if (StageCount >= MaxStages) {
        ++Dropped;
        return;
    }
    StageRow& row = Stages[StageCount++];
    row.StageId = stageId;
    if (!AddDuration(row.TotalMicroseconds, row.Calls, elapsedMicroseconds)) {
        DiscardWindow(ResetReason::Overflow);
    }
}

void Ps1CpuProfileWindow::AddComponent(std::uintptr_t identity, std::uint32_t index,
                                       std::uint32_t typeHash, std::uint32_t ownerSceneEntityId,
                                       std::uint64_t elapsedMicroseconds) {
    if (!IsFrameUsable()) {
        return;
    }
    if (CurrentComponentCount >= MaxComponents) {
        ++Dropped;
        return;
    }

    ComponentKey& key = CurrentComponents[CurrentComponentCount++];
    key.Identity = identity;
    key.Index = index;
    key.TypeHash = typeHash;
    key.OwnerSceneEntityId = ownerSceneEntityId;

    for (std::size_t i = 0; i < ComponentCount; ++i) {
        if (Components[i].Identity == identity) {
            if (!AddDuration(Components[i].TotalMicroseconds, Components[i].Calls, elapsedMicroseconds)) {
                DiscardWindow(ResetReason::Overflow);
            }
            return;
        }
    }
    if (ComponentCount >= MaxComponents) {
        ++Dropped;
        return;
    }
    ComponentRow& row = Components[ComponentCount++];
    row.Identity = identity;
    row.Index = index;
    row.TypeHash = typeHash;
    row.OwnerSceneEntityId = ownerSceneEntityId;
    if (!AddDuration(row.TotalMicroseconds, row.Calls, elapsedMicroseconds)) {
        DiscardWindow(ResetReason::Overflow);
    }
}

void Ps1CpuProfileWindow::EndFrame(std::uint64_t sequence, std::uint32_t vblank) {
    if (!IsFrameUsable()) {
        return;
    }
    if (sequence != CurrentSequence) {
        DiscardWindow(ResetReason::SequenceGap);
        return;
    }

    if (!HaveBaseline) {
        BaselineComponentCount = CurrentComponentCount;
        std::memcpy(BaselineComponents, CurrentComponents, sizeof(CurrentComponents));
        HaveBaseline = true;
    } else if (CurrentComponentCount != BaselineComponentCount) {
        DiscardWindow(ResetReason::MembershipChanged);
        return;
    } else {
        for (std::size_t i = 0; i < BaselineComponentCount; ++i) {
            if (!KeysEqual(BaselineComponents[i], CurrentComponents[i])) {
                DiscardWindow(ResetReason::MembershipChanged);
                return;
            }
        }
    }

    if (FrameCount == 0u) {
        FirstSequence = sequence;
        FirstVblank = vblank;
    }
    ++FrameCount;
    LastSequenceValue = sequence;
    LastVblankValue = vblank;
    PreviousSequence = sequence;
    HavePreviousSequence = true;
    FrameActive = false;
    if (FrameCount >= 600u) {
        ReportReady = true;
    }
}

bool Ps1CpuProfileWindow::AddTypeLegend(std::uint32_t typeHash, const char* name) {
    if (name == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < TypeLegendCount; ++i) {
        if (TypeLegends[i].TypeHash == typeHash) {
            return true;
        }
    }
    if (TypeLegendCount >= MaxTypeLegends) {
        ++Dropped;
        return false;
    }
    TypeLegend& legend = TypeLegends[TypeLegendCount++];
    legend.TypeHash = typeHash;
    std::size_t length = std::strlen(name);
    if (length >= sizeof(legend.Name)) {
        length = sizeof(legend.Name) - 1u;
    }
    std::memcpy(legend.Name, name, length);
    legend.Name[length] = '\0';
    return true;
}

void Ps1CpuProfileWindow::ClearReport() {
    ResetStorage();
}

Ps1CpuProfileWindow::ReportView Ps1CpuProfileWindow::GetReport() const {
    ReportView report;
    report.Ready = ReportReady;
    report.FirstSequence = FirstSequence;
    report.LastSequence = LastSequenceValue;
    report.FirstVblank = FirstVblank;
    report.LastVblank = LastVblankValue;
    report.SceneId = SceneId;
    report.FrameCount = FrameCount;
    report.Dropped = Dropped;
    report.Resets = Resets;
    report.ClockFaults = ClockFaults;
    report.StageCount = StageCount;
    report.ComponentCount = ComponentCount;
    report.TypeLegendCount = TypeLegendCount;
    report.Stages = Stages;
    report.Components = Components;
    report.TypeLegends = TypeLegends;
    return report;
}

bool Ps1CpuProfileWindow::TryFormatMilliseconds(std::uint64_t totalMicroseconds,
                                                 std::uint32_t calls,
                                                 char (&out)[16]) {
    if (calls == 0u) {
        out[0] = '\0';
        return false;
    }
    const std::uint64_t averageMicroseconds = totalMicroseconds / calls;
    const std::uint64_t wholeMilliseconds = averageMicroseconds / 1000u;
    const std::uint32_t fraction = static_cast<std::uint32_t>(averageMicroseconds % 1000u);

    char digits[20];
    std::size_t digitCount = 0;
    std::uint64_t value = wholeMilliseconds;
    do {
        digits[digitCount++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && digitCount < sizeof(digits));
    if (digitCount + 5u > 16u) {
        out[0] = '\0';
        return false;
    }
    std::size_t position = 0;
    while (digitCount != 0u) {
        out[position++] = digits[--digitCount];
    }
    out[position++] = '.';
    out[position++] = static_cast<char>('0' + (fraction / 100u));
    out[position++] = static_cast<char>('0' + ((fraction / 10u) % 10u));
    out[position++] = static_cast<char>('0' + (fraction % 10u));
    out[position] = '\0';
    return true;
}

} // namespace helengine::ps1
