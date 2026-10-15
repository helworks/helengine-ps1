#pragma once

#include "IContentStreamSource.hpp"
#include "IRuntimeSceneContentProvider.hpp"
#include "platform/ps1/modules/Ps1DiscFileLocator.hpp"
#include "platform/ps1/modules/Ps1DiscSectorReader.hpp"
#include "platform/ps1/modules/Ps1Sha256.hpp"

namespace helengine::ps1 {

/// <summary>Cooked dependency range and digest bound to the packaged asset archive.</summary>
struct Ps1DiscAssetDescriptor {
    const char* RelativePath;
    std::uint32_t ArchiveOffset;
    std::uint32_t ByteLength;
    std::uint8_t Sha256[32];
};

/// <summary>Complete, deduplicated dependency list for one catalog scene, including later dynamic loads.</summary>
struct Ps1DiscSceneDependencies {
    const char* SceneId;
    const std::uint16_t* AssetIndices;
    std::uint32_t AssetCount;
};

/// <summary>One resident cache record; scene leases and streams retain bytes independently.</summary>
struct Ps1DiscCachedAsset {
    std::uint8_t* Bytes=nullptr;
    const void* LoadingOwner=nullptr;
    std::uint32_t SceneReferences=0;
    std::uint32_t StreamReferences=0;
    bool Ready=false;
};

#if defined(__mips__)
static_assert(sizeof(Ps1DiscCachedAsset)==20, "Measured PS1 cache record ABI changed");
#endif

/// <summary>Memory-only content source and cooperative prefetch provider sharing one asynchronous CD drive.</summary>
class Ps1DiscContent final : public ::IContentStreamSource, public ::IRuntimeSceneContentProvider {
public:
    Ps1DiscContent(Ps1DiscSectorReader& reader,Ps1DiscFileLocator& locator,
                   const Ps1DiscAssetDescriptor* assets,std::uint32_t assetCount,
                   const Ps1DiscSceneDependencies* scenes,std::uint32_t sceneCount,
                   std::uint32_t archiveBytes,std::uint32_t cacheBudgetBytes);
    ~Ps1DiscContent();
    ::Stream* OpenRead(HeCppString path) override;
    ::IRuntimeSceneContentLoadOperation* BeginPreloadScene(::RuntimeSceneCatalogEntry* entry) override;

    void RetainScene(std::uint16_t index);
    void ReleaseScene(std::uint16_t index);
    void ReleaseStream(std::uint16_t index);
    bool PrepareArchive(const void* owner,bool& requested);
    bool PrepareAsset(const void* owner,std::uint16_t index,std::uint32_t& position,bool& requested,Ps1Sha256& hash);
    void Cancel(const void* owner);
    std::uint32_t GetCacheBytes() const { return CacheBytes; }

private:
    void DropUnused(std::uint16_t index);
    Ps1DiscSectorReader& Reader;
    Ps1DiscFileLocator& Locator;
    const Ps1DiscAssetDescriptor* Assets;
    std::uint32_t AssetCount;
    const Ps1DiscSceneDependencies* Scenes;
    std::uint32_t SceneCount;
    Ps1DiscCachedAsset* Cached=nullptr;
    std::uint32_t ArchiveBytes;
    std::uint32_t ArchiveFirstSector=0;
    std::uint32_t CacheBudget;
    std::uint32_t CacheBytes=0;
    std::uint32_t ReservedBytes=0;
    const void* ArchiveOwner=nullptr;
    bool ArchiveReady=false;
};

}  // namespace helengine::ps1
