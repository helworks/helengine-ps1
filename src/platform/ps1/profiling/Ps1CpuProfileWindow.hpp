#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

/** Fixed-capacity, allocation-free CPU timing window for native host profiling. */
class Ps1CpuProfileWindow final {
public:
    static constexpr std::size_t MaxComponents = 128;
    static constexpr std::size_t MaxStages = 32;
    static constexpr std::size_t MaxTypeLegends = 64;

    enum class ResetReason : std::uint8_t {
        Explicit,
        SequenceGap,
        SceneChanged,
        MembershipChanged,
        ClockFault,
        Overflow,
        InvalidFrame,
    };

    struct StageRow {
        std::uint32_t StageId = 0;
        std::uint64_t TotalMicroseconds = 0;
        std::uint32_t Calls = 0;
    };

    struct ComponentRow {
        std::uintptr_t Identity = 0;
        std::uint32_t Index = 0;
        std::uint32_t TypeHash = 0;
        std::uint32_t OwnerSceneEntityId = 0;
        std::uint64_t TotalMicroseconds = 0;
        std::uint32_t Calls = 0;
    };

    struct TypeLegend {
        std::uint32_t TypeHash = 0;
        char Name[48]{};
    };

    struct ReportView {
        bool Ready = false;
        std::uint64_t FirstSequence = 0;
        std::uint64_t LastSequence = 0;
        std::uint32_t FirstVblank = 0;
        std::uint32_t LastVblank = 0;
        std::uint32_t SceneId = 0;
        std::uint32_t FrameCount = 0;
        std::uint32_t Dropped = 0;
        std::uint32_t Resets = 0;
        std::uint32_t ClockFaults = 0;
        std::size_t StageCount = 0;
        std::size_t ComponentCount = 0;
        std::size_t TypeLegendCount = 0;
        const StageRow* Stages = nullptr;
        const ComponentRow* Components = nullptr;
        const TypeLegend* TypeLegends = nullptr;
    };

    void BeginFrame(std::uint64_t sequence, std::uint32_t vblank, std::uint32_t sceneId);
    void AddStage(std::uint32_t stageId, std::uint64_t elapsedMicroseconds);
    void AddComponent(std::uintptr_t identity, std::uint32_t index, std::uint32_t typeHash,
                      std::uint32_t ownerSceneEntityId, std::uint64_t elapsedMicroseconds);
    void EndFrame(std::uint64_t sequence, std::uint32_t vblank);
    void ClockFault();
    void Reset(ResetReason reason = ResetReason::Explicit);

    bool AddTypeLegend(std::uint32_t typeHash, const char* name);
    void ClearReport();

    bool IsReportReady() const { return ReportReady; }
    ReportView GetReport() const;
    std::uint32_t DroppedCount() const { return Dropped; }
    std::uint32_t ResetCount() const { return Resets; }
    std::uint32_t ClockFaultCount() const { return ClockFaults; }
    ResetReason LastReset() const { return LastResetReason; }

    static bool TryFormatMilliseconds(std::uint64_t totalMicroseconds, std::uint32_t calls,
                                      char (&out)[16]);

private:
    struct ComponentKey {
        std::uintptr_t Identity = 0;
        std::uint32_t Index = 0;
        std::uint32_t TypeHash = 0;
        std::uint32_t OwnerSceneEntityId = 0;
    };

    static bool KeysEqual(const ComponentKey& left, const ComponentKey& right);
    static bool AddDuration(std::uint64_t& total, std::uint32_t& calls, std::uint64_t elapsed);
    void ResetStorage();
    void DiscardWindow(ResetReason reason);
    bool IsFrameUsable() const { return FrameActive && !FrameDiscarded; }

    StageRow Stages[MaxStages]{};
    ComponentRow Components[MaxComponents]{};
    TypeLegend TypeLegends[MaxTypeLegends]{};
    ComponentKey BaselineComponents[MaxComponents]{};
    ComponentKey CurrentComponents[MaxComponents]{};

    std::size_t StageCount = 0;
    std::size_t ComponentCount = 0;
    std::size_t TypeLegendCount = 0;
    std::size_t BaselineComponentCount = 0;
    std::size_t CurrentComponentCount = 0;

    bool FrameActive = false;
    bool FrameDiscarded = false;
    bool HaveBaseline = false;
    bool HavePreviousSequence = false;
    bool ReportReady = false;
    std::uint64_t CurrentSequence = 0;
    std::uint64_t PreviousSequence = 0;
    std::uint64_t FirstSequence = 0;
    std::uint64_t LastSequenceValue = 0;
    std::uint32_t CurrentVblank = 0;
    std::uint32_t FirstVblank = 0;
    std::uint32_t LastVblankValue = 0;
    std::uint32_t SceneId = 0;
    std::uint32_t FrameCount = 0;
    std::uint32_t Dropped = 0;
    std::uint32_t Resets = 0;
    std::uint32_t ClockFaults = 0;
    ResetReason LastResetReason = ResetReason::Explicit;
};

} // namespace helengine::ps1
