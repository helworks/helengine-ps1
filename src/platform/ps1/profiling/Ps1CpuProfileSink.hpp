#pragma once

#include <cstdint>

#include "IRuntimeCpuProfileSink.hpp"
#include "platform/ps1/profiling/Ps1CpuProfileWindow.hpp"

namespace helengine::ps1 {

/// Bridges generated Core CPU probes to the bounded PS1 report window.
class Ps1CpuProfileSink final : public ::IRuntimeCpuProfileSink {
public:
    enum NativeStageId : std::uint32_t {
        CameraSetup = 100u,
        Mesh3D = 101u,
        UiQueueVisit = 102u,
        UiPacketFlush = 103u,
        Submit = 104u,
    };

    std::uint64_t ReadMicroseconds() override;
    void RecordStage(::RuntimeCpuProfileStage stage, std::uint64_t elapsedMicroseconds) override;
    void RecordUpdateable(::IUpdateable* item, int index, std::uint32_t typeHash,
                          std::uint32_t ownerSceneEntityId, std::uint64_t elapsedMicroseconds) override;
    void RegisterType(std::uint32_t typeHash, HeCppString typeName) override;
    void ClockFault() override;

    void BeginFrame(std::uint64_t sequence, std::uint32_t vblank, const char* sceneId);
    void EndFrame(std::uint64_t sequence, std::uint32_t vblank);
    void RecordNativeStage(std::uint32_t stageId, std::uint64_t elapsedMicroseconds);
    void RecordHostTimings(std::uint64_t updateMicroseconds, std::uint64_t boundaryMicroseconds,
                           std::uint64_t drawMicroseconds, std::uint64_t betweenMicroseconds);
    bool HasPendingReport() const { return Window.IsReportReady(); }
    void EmitPendingReport();

private:
    static const char* CoreStageName(std::uint32_t stageId);
    static const char* CoreStageParent(std::uint32_t stageId);
    static std::uint32_t SceneHash(const char* sceneId);
    const char* FindTypeName(std::uint32_t typeHash, const Ps1CpuProfileWindow::ReportView& report) const;

    Ps1CpuProfileWindow Window;
    const char* SceneId = "unknown";
    std::uint64_t HostUpdateTotalUs = 0;
    std::uint64_t HostBoundaryTotalUs = 0;
    std::uint64_t HostDrawTotalUs = 0;
    std::uint64_t HostBetweenTotalUs = 0;
    std::uint32_t HostFrameCount = 0;
    std::uint32_t ObservedResetCount = 0;
};

} // namespace helengine::ps1
