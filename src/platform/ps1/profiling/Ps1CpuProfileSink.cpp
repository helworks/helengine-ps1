#include "platform/ps1/profiling/Ps1CpuProfileSink.hpp"

// Numeric guest-memory counters let a host profiler inspect native work without
// BIOS logging stalls or screen capture. They are linked only in CPU-profile builds.
extern "C" {
volatile std::uint64_t helengine_ps1_profile_native_us[128]{};
volatile std::uint32_t helengine_ps1_profile_native_calls[128]{};
}
#include "IUpdateable.hpp"
#include "SceneManager.hpp"
#include "LoadedSceneRecord.hpp"

#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "common/syscalls/syscalls.h"
#include <typeinfo>

namespace helengine::ps1 {

std::uint64_t Ps1CpuProfileSink::ReadMicroseconds() {
    return he_cpp_custom::MonotonicMicroseconds();
}

void Ps1CpuProfileSink::RecordStage(::RuntimeCpuProfileStage stage, std::uint64_t elapsedMicroseconds) {
    RecordNativeStage(static_cast<std::uint32_t>(stage), elapsedMicroseconds);
}

void Ps1CpuProfileSink::RecordUpdateable(::IUpdateable* item, std::int32_t index, std::uint32_t typeHash,
                                         std::uint32_t ownerSceneEntityId,
                                         std::uint64_t elapsedMicroseconds) {
    Window.AddComponent(reinterpret_cast<std::uintptr_t>(item), static_cast<std::uint32_t>(index),
                        ResolveRegisteredTypeHash(item, typeHash), ownerSceneEntityId, elapsedMicroseconds);
}

void Ps1CpuProfileSink::RegisterType(::IUpdateable* item, std::uint32_t typeHash, HeCppString typeName) {
    if (item == nullptr) return;
    (void)typeHash;

    char decodedName[48]{};
    const char* nativeName = typeid(*item).name();
    const bool decoded = DecodeNativeTypeName(nativeName, decodedName);
    if (!decoded) {
        const char* managedName = typeName.c_str();
        std::size_t length = 0;
        while (managedName != nullptr && length + 1u < sizeof(decodedName) && managedName[length] != '\0') {
            decodedName[length] = managedName[length];
            ++length;
        }
        decodedName[length] = '\0';
    }

    const std::uint32_t nativeHash = StableTypeNameHash(decodedName);
    const std::uintptr_t identity = reinterpret_cast<std::uintptr_t>(item);
    for (std::size_t i = 0; i < TypeRegistrationCount; ++i) {
        if (TypeRegistrations[i].Identity == identity) {
            TypeRegistrations[i].TypeHash = nativeHash;
            Window.AddTypeLegend(nativeHash, decodedName);
            return;
        }
    }
    if (TypeRegistrationCount < MaxTypeRegistrations) {
        TypeRegistrations[TypeRegistrationCount++] = TypeRegistration{identity, nativeHash};
    }
    Window.AddTypeLegend(nativeHash, decodedName);
}

void Ps1CpuProfileSink::ClockFault() {
    Window.ClockFault();
}

void Ps1CpuProfileSink::BeginFrame(std::uint64_t sequence, std::uint32_t vblank, ::SceneManager* sceneManager) {
    ::LoadedSceneRecord* record = nullptr;
    std::int32_t kind = 0;
    std::int32_t loadedCount = 0;
    if (sceneManager == nullptr) { kind = 1; }
    else if (sceneManager->get_IsSceneTransitionActive() || sceneManager->get_HasPendingSceneOperations()) { kind = 2; }
    else {
        auto* loaded = sceneManager->get_LoadedScenes();
        loadedCount = loaded->get_Count();
        if (loadedCount == 1) { record = loaded->get_Item(0); }
        else { kind = loadedCount == 0 ? 3 : 4; }
    }
    if (record != ProfiledScene || kind != ProfiledSceneKind
        || (record != nullptr && record->get_SceneId() != SceneId)) {
        ProfiledScene = record;
        ProfiledSceneKind = kind;
        SceneId = record != nullptr ? record->get_SceneId()
            : HeCppString(kind == 1 ? "no-scene-manager" : kind == 2 ? "scene-loading"
                : kind == 3 ? "no-loaded-scene" : "multiple-loaded-scenes");
        ProfiledSceneHash = SceneHash(SceneId.c_str());
        ramsyscall_printf("HE_CPU_SCENE id=%s loaded=%u transition=%u\n", SceneId.c_str(),
                          static_cast<unsigned>(loadedCount), kind == 2 ? 1u : 0u);
    }
    Window.BeginFrame(sequence, vblank, ProfiledSceneHash);
}

void Ps1CpuProfileSink::EndFrame(std::uint64_t sequence, std::uint32_t vblank) {
    Window.EndFrame(sequence, vblank);
}

void Ps1CpuProfileSink::RecordNativeStage(std::uint32_t stageId, std::uint64_t elapsedMicroseconds) {
    if (stageId < 128u) {
        helengine_ps1_profile_native_us[stageId] = helengine_ps1_profile_native_us[stageId] + elapsedMicroseconds;
        helengine_ps1_profile_native_calls[stageId] = helengine_ps1_profile_native_calls[stageId] + 1u;
    }
    Window.AddStage(stageId, elapsedMicroseconds);
}

void Ps1CpuProfileSink::RecordHostTimings(std::uint64_t updateMicroseconds,
                                          std::uint64_t boundaryMicroseconds,
                                          std::uint64_t drawMicroseconds,
                                          std::uint64_t betweenMicroseconds) {
    const Ps1CpuProfileWindow::ReportView current = Window.GetReport();
    if (current.Resets != ObservedResetCount) {
        HostUpdateTotalUs = 0;
        HostBoundaryTotalUs = 0;
        HostDrawTotalUs = 0;
        HostBetweenTotalUs = 0;
        HostFrameCount = 0;
        ObservedResetCount = current.Resets;
    }
    if (HostFrameCount != 0u) {
        HostBetweenTotalUs += betweenMicroseconds;
    }
    HostUpdateTotalUs += updateMicroseconds;
    HostBoundaryTotalUs += boundaryMicroseconds;
    HostDrawTotalUs += drawMicroseconds;
    ++HostFrameCount;
}
std::uint32_t Ps1CpuProfileSink::SceneHash(const char* sceneId) {
    std::uint32_t hash = 2166136261u;
    if (sceneId == nullptr) return hash;
    while (*sceneId != '\0') {
        hash ^= static_cast<std::uint8_t>(*sceneId++);
        hash *= 16777619u;
    }
    return hash;
}

std::uint32_t Ps1CpuProfileSink::StableTypeNameHash(const char* typeName) {
    std::uint32_t hash = 2166136261u;
    if (typeName == nullptr) return hash;
    while (*typeName != '\0') {
        hash ^= static_cast<std::uint8_t>(*typeName++);
        hash *= 16777619u;
    }
    return hash;
}

bool Ps1CpuProfileSink::DecodeNativeTypeName(const char* encoded, char (&out)[48]) {
    if (encoded == nullptr || *encoded == '\0') return false;
    const char* cursor = encoded;
    const char* lastSegment = nullptr;
    std::size_t lastLength = 0;
    if (*cursor == 'N') ++cursor;
    while (*cursor != '\0' && *cursor != 'E') {
        if (*cursor < '0' || *cursor > '9') return false;
        std::size_t length = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            length = length * 10u + static_cast<std::size_t>(*cursor - '0');
            ++cursor;
        }
        if (length == 0u || cursor[length - 1u] == '\0') return false;
        lastSegment = cursor;
        lastLength = length;
        cursor += length;
    }
    if (lastSegment == nullptr || lastLength == 0u || lastLength >= sizeof(out)) return false;
    for (std::size_t i = 0; i < lastLength; ++i) out[i] = lastSegment[i];
    out[lastLength] = '\0';
    return true;
}

std::uint32_t Ps1CpuProfileSink::ResolveRegisteredTypeHash(::IUpdateable* item,
                                                           std::uint32_t fallback) const {
    const std::uintptr_t identity = reinterpret_cast<std::uintptr_t>(item);
    for (std::size_t i = 0; i < TypeRegistrationCount; ++i) {
        if (TypeRegistrations[i].Identity == identity) return TypeRegistrations[i].TypeHash;
    }
    return fallback;
}

const char* Ps1CpuProfileSink::CoreStageName(std::uint32_t stageId) {
    switch (stageId) {
        case 1u: return "early_input";
        case 2u: return "frame_counters";
        case 3u: return "object_manager";
        case 4u: return "audio";
        case 5u: return "physics";
        case 6u: return "late_input";
        case 7u: return "pointer_interaction";
        case 8u: return "frame_boundary";
        case 9u: return "render_manager_3d";
        case CameraSetup: return "camera_setup";
        case Mesh3D: return "mesh_3d";
        case UiQueueVisit: return "ui_queue_visit";
        case UiPacketFlush: return "ui_packet_flush";
        case Submit: return "submit";
        case UiTextDraw: return "ui_text_draw";
        case UiSpriteDraw: return "ui_sprite_draw";
        case UiShapeDraw: return "ui_shape_draw";
        case UiSpritePacket: return "ui_sprite_packet";
        case UiShapePacket: return "ui_shape_packet";
        case UiSpriteGeometry: return "ui_sprite_geometry";
        default: return "unknown";
    }
}

const char* Ps1CpuProfileSink::CoreStageParent(std::uint32_t stageId) {
    if (stageId >= CameraSetup || stageId == 9u) return "draw";
    if (stageId >= 1u && stageId <= 7u) return "update";
    if (stageId == 8u) return "boundary";
    return "core";
}

const char* Ps1CpuProfileSink::FindTypeName(std::uint32_t typeHash,
                                             const Ps1CpuProfileWindow::ReportView& report) const {
    for (std::size_t i = 0; i < report.TypeLegendCount; ++i) {
        if (report.TypeLegends[i].TypeHash == typeHash) return report.TypeLegends[i].Name;
    }
    return "unknown";
}

void Ps1CpuProfileSink::EmitPendingReport() {
    if (!Window.IsReportReady()) return;
    const Ps1CpuProfileWindow::ReportView report = Window.GetReport();
    char updateMilliseconds[16]{};
    char boundaryMilliseconds[16]{};
    char drawMilliseconds[16]{};
    char betweenMilliseconds[16]{};
    Ps1CpuProfileWindow::TryFormatMilliseconds(HostUpdateTotalUs, HostFrameCount, updateMilliseconds);
    Ps1CpuProfileWindow::TryFormatMilliseconds(HostBoundaryTotalUs, HostFrameCount, boundaryMilliseconds);
    Ps1CpuProfileWindow::TryFormatMilliseconds(HostDrawTotalUs, HostFrameCount, drawMilliseconds);
    Ps1CpuProfileWindow::TryFormatMilliseconds(HostBetweenTotalUs, HostFrameCount, betweenMilliseconds);
    ramsyscall_printf("HE_CPU_WINDOW platform=ps1 scene=%s build=cpu_profile frames=%u first=%u last=%u clock=hsync64us update_ms=%s draw_ms=%s boundary_ms=%s between_ms=%s scene_hash=%08x dropped=%u resets=%u vblank_first=%u vblank_last=%u\n",
                      SceneId.c_str(), static_cast<unsigned>(report.FrameCount), static_cast<unsigned>(report.FirstSequence),
                      static_cast<unsigned>(report.LastSequence), updateMilliseconds, drawMilliseconds,
                      boundaryMilliseconds, betweenMilliseconds, static_cast<unsigned>(report.SceneId),
                      static_cast<unsigned>(report.Dropped), static_cast<unsigned>(report.Resets),
                      static_cast<unsigned>(report.FirstVblank), static_cast<unsigned>(report.LastVblank));
    char milliseconds[16]{};
    for (std::size_t i = 0; i < report.StageCount; ++i) {
        const Ps1CpuProfileWindow::StageRow& row = report.Stages[i];
        if (!Ps1CpuProfileWindow::TryFormatMilliseconds(row.TotalMicroseconds, row.Calls, milliseconds)) continue;
        ramsyscall_printf("HE_CPU_STAGE parent=%s name=%s id=%u avg_ms=%s calls=%u\n",
                          CoreStageParent(row.StageId), CoreStageName(row.StageId), static_cast<unsigned>(row.StageId),
                          milliseconds, static_cast<unsigned>(row.Calls));
    }
    for (std::size_t i = 0; i < report.ComponentCount; ++i) {
        const Ps1CpuProfileWindow::ComponentRow& row = report.Components[i];
        if (!Ps1CpuProfileWindow::TryFormatMilliseconds(row.TotalMicroseconds, row.Calls, milliseconds)) continue;
        ramsyscall_printf("HE_CPU_COMPONENT index=%u type=%s hash=%08x owner=%u identity=%p avg_ms=%s calls=%u\n",
                          static_cast<unsigned>(row.Index), FindTypeName(row.TypeHash, report),
                          static_cast<unsigned>(row.TypeHash), static_cast<unsigned>(row.OwnerSceneEntityId),
                          reinterpret_cast<const void*>(row.Identity), milliseconds, static_cast<unsigned>(row.Calls));
    }
    ramsyscall_printf("HE_CPU_WINDOW_END dropped=%u resets=%u clock_faults=%u vblank_first=%u vblank_last=%u\n",
                      static_cast<unsigned>(report.Dropped), static_cast<unsigned>(report.Resets),
                      static_cast<unsigned>(report.ClockFaults), static_cast<unsigned>(report.FirstVblank),
                      static_cast<unsigned>(report.LastVblank));
    Window.ClearReport();
    HostUpdateTotalUs = 0;
    HostBoundaryTotalUs = 0;
    HostDrawTotalUs = 0;
    HostBetweenTotalUs = 0;
    HostFrameCount = 0;
}

} // namespace helengine::ps1
