#include "platform/ps1/modules/Ps1DiscContent.hpp"
#include "IRuntimeSceneContentLoadOperation.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "platform/ps1/Ps1EmbeddedAssetStream.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"

namespace helengine::ps1 {
namespace {

class PinnedStream final : public ::Stream {
public:
    PinnedStream(Ps1DiscContent& cache,std::uint16_t index,const std::uint8_t* data,std::size_t bytes)
        : Cache(cache),Index(index),View(data,bytes) {}
    ~PinnedStream() override { Dispose(); }
    std::size_t Read(std::uint8_t* b,std::size_t offset,std::size_t count) override { RequireLive();return View.Read(b,offset,count); }
    void Write(const std::uint8_t* b,std::size_t offset,std::size_t count) override { RequireLive();View.Write(b,offset,count); }
    std::size_t Seek(std::int64_t offset,SeekOrigin origin) override { RequireLive();return View.Seek(offset,origin); }
    void SetLength(std::size_t bytes) override { RequireLive();View.SetLength(bytes); }
    bool CanRead() const override { return !Disposed; }
    bool CanWrite() const override { return false; }
    bool CanSeek() const override { return !Disposed; }
    std::size_t Length() const override { RequireLive();return View.Length(); }
    std::size_t Position() const override { RequireLive();return View.Position(); }
    void SetPosition(std::size_t value) override { RequireLive();View.SetPosition(value); }
    void InternalReserve(std::size_t bytes) override { RequireLive();View.InternalReserve(bytes); }
    void InternalWriteByte(std::uint8_t value) override { RequireLive();View.InternalWriteByte(value); }
    int InternalReadByte() override { RequireLive();return View.InternalReadByte(); }
    void Dispose() override { if (!Disposed) { Cache.ReleaseStream(Index);Disposed=true; } }
    void Close() override { Dispose(); }
private:
    void RequireLive() const { if (Disposed) he_cpp_custom::Fail("Disc content stream used after disposal"); }
    Ps1DiscContent& Cache;
    std::uint16_t Index;
    Ps1EmbeddedAssetStream View;
    bool Disposed=false;
};

class ContentPreload final : public ::IRuntimeSceneContentLoadOperation {
public:
    ContentPreload(Ps1DiscContent& cache,const Ps1DiscSceneDependencies& scene) : Cache(cache),Scene(scene) {
        for (std::uint32_t i=0;i<Scene.AssetCount;++i) Cache.RetainScene(Scene.AssetIndices[i]);
    }
    ~ContentPreload() override { Dispose(); }
    bool get_IsCompleted() override { return Completed; }
    void Advance() override {
        if (Disposed || Completed) return;
        if (!ArchivePrepared) { ArchivePrepared=Cache.PrepareArchive(this,Requested);return; }
        if (Index==Scene.AssetCount) { Completed=true;return; }
        if (Cache.PrepareAsset(this,Scene.AssetIndices[Index],Position,Requested,Hash)) {
            ++Index;Position=0;Requested=false;Hash=Ps1Sha256();
        }
    }
    void Dispose() override {
        if (Disposed) return;
        Cache.Cancel(this);
        for (std::uint32_t i=0;i<Scene.AssetCount;++i) Cache.ReleaseScene(Scene.AssetIndices[i]);
        Disposed=true;
    }
private:
    Ps1DiscContent& Cache;
    const Ps1DiscSceneDependencies& Scene;
    Ps1Sha256 Hash;
    std::uint32_t Index=0;
    std::uint32_t Position=0;
    bool Requested=false;
    bool ArchivePrepared=false;
    bool Completed=false;
    bool Disposed=false;
};
}

Ps1DiscContent::Ps1DiscContent(Ps1DiscSectorReader& reader,Ps1DiscFileLocator& locator,
    const Ps1DiscAssetDescriptor* assets,std::uint32_t assetCount,const Ps1DiscSceneDependencies* scenes,
    std::uint32_t sceneCount,std::uint32_t archiveBytes,std::uint32_t cacheBudgetBytes)
    : Reader(reader),Locator(locator),Assets(assets),AssetCount(assetCount),Scenes(scenes),SceneCount(sceneCount),
      ArchiveBytes(archiveBytes),CacheBudget(cacheBudgetBytes) {
    if (assetCount>UINT16_MAX || (assetCount!=0 && assets==nullptr) || (sceneCount!=0 && scenes==nullptr) ||
        assetCount>cacheBudgetBytes/sizeof(Ps1DiscCachedAsset)) he_cpp_custom::Fail("Disc content table exceeds declared cache budget");
    CacheBytes=assetCount*sizeof(Ps1DiscCachedAsset);
    ReservedBytes=CacheBytes;
    Cached=new Ps1DiscCachedAsset[assetCount];
    for (std::uint32_t i=0;i<assetCount;++i) {
        const auto& asset=assets[i];
        if (asset.RelativePath==nullptr || asset.ByteLength==0 || asset.ArchiveOffset>archiveBytes ||
            asset.ByteLength>archiveBytes-asset.ArchiveOffset) he_cpp_custom::Fail("Disc asset table range invalid");
        for (std::uint32_t j=0;j<i;++j) if (__builtin_strcmp(asset.RelativePath,assets[j].RelativePath)==0)
            he_cpp_custom::Fail("Disc asset table contains duplicate paths");
    }
    for (std::uint32_t i=0;i<sceneCount;++i) {
        const auto& scene=scenes[i];
        if (scene.SceneId==nullptr || (scene.AssetCount!=0 && scene.AssetIndices==nullptr) || scene.AssetCount>assetCount)
            he_cpp_custom::Fail("Disc scene dependency table invalid");
        for (std::uint32_t j=0;j<scene.AssetCount;++j) {
            if (scene.AssetIndices[j]>=assetCount) he_cpp_custom::Fail("Disc scene dependency index outside table");
            for (std::uint32_t k=0;k<j;++k) if (scene.AssetIndices[j]==scene.AssetIndices[k])
                he_cpp_custom::Fail("Disc scene dependency duplicated");
        }
    }
}

Ps1DiscContent::~Ps1DiscContent() {
    for (std::uint32_t i=0;i<AssetCount;++i) {
        if (Cached[i].SceneReferences!=0 || Cached[i].StreamReferences!=0 || Cached[i].LoadingOwner!=nullptr)
            he_cpp_custom::Fail("Disc cache destroyed with live leases");
        delete[] Cached[i].Bytes;
    }
    delete[] Cached;
}

::IRuntimeSceneContentLoadOperation* Ps1DiscContent::BeginPreloadScene(::RuntimeSceneCatalogEntry* entry) {
    if (entry==nullptr) he_cpp_custom::Fail("Disc scene preload entry missing");
    for (std::uint32_t i=0;i<SceneCount;++i) if (entry->get_SceneId()==Scenes[i].SceneId) {
        std::uint32_t required=ReservedBytes;
        for (std::uint32_t j=0;j<Scenes[i].AssetCount;++j) {
            const auto index=Scenes[i].AssetIndices[j];
            const auto& item=Cached[index];
            if (item.SceneReferences==0 && item.StreamReferences==0) {
                if (Assets[index].ByteLength>CacheBudget-required) he_cpp_custom::Fail("Whole scene dependency cache exceeds configured retail RAM budget");
                required+=Assets[index].ByteLength;
            }
        }
        return new ContentPreload(*this,Scenes[i]);
    }
    he_cpp_custom::Fail("Scene has no complete packaged dependency table");
}

bool Ps1DiscContent::PrepareArchive(const void* owner,bool& requested) {
    if (ArchiveReady) return true;
    if (ArchiveOwner!=nullptr && ArchiveOwner!=owner) return false;
    if (!requested) { requested=Locator.BeginFind(owner,"ASSETS.BIN;1");if (requested) ArchiveOwner=owner;return false; }
    Ps1DiscFileRange file;bool failed=false;
    if (!Locator.PollFind(owner,file,failed)) return false;
    Reader.Consume(owner);requested=false;ArchiveOwner=nullptr;
    if (failed || file.ByteLength!=ArchiveBytes || file.FirstSector>UINT32_MAX-(ArchiveBytes-1)/2048)
        he_cpp_custom::Fail("Packaged disc asset archive length/range does not match build");
    ArchiveFirstSector=file.FirstSector;ArchiveReady=true;return true;
}

bool Ps1DiscContent::PrepareAsset(const void* owner,std::uint16_t index,std::uint32_t& position,bool& requested,Ps1Sha256& hash) {
    auto& item=Cached[index];const auto& descriptor=Assets[index];
    if (item.Ready) return true;
    if (item.LoadingOwner!=nullptr && item.LoadingOwner!=owner) return false;
    if (item.Bytes==nullptr) {
        if (descriptor.ByteLength>CacheBudget-CacheBytes) he_cpp_custom::Fail("Scene dependency cache exceeds configured retail RAM budget");
        item.Bytes=new std::uint8_t[descriptor.ByteLength];CacheBytes+=descriptor.ByteLength;item.LoadingOwner=owner;
    }
    const std::uint32_t absolute=descriptor.ArchiveOffset+position;
    if (!requested) { requested=Reader.Begin(owner,ArchiveFirstSector+absolute/2048);return false; }
    bool failed=false;const auto* sector=Reader.Poll(owner,failed);
    if (sector==nullptr) return false;
    if (failed) he_cpp_custom::Fail("Disc content sector read failed");
    const std::uint32_t skip=absolute%2048;
    const std::uint32_t remaining=descriptor.ByteLength-position;
    const std::uint32_t count=remaining<2048-skip?remaining:2048-skip;
    __builtin_memcpy(item.Bytes+position,sector+skip,count);hash.Update(sector+skip,count);
    Reader.Consume(owner);requested=false;position+=count;
    if (position!=descriptor.ByteLength) return false;
    std::uint8_t digest[32];hash.Finish(digest);
    if (!Ps1Sha256::Equal(digest,descriptor.Sha256)) he_cpp_custom::Fail("Disc content digest does not match packaged dependency");
    item.Ready=true;item.LoadingOwner=nullptr;return true;
}

void Ps1DiscContent::RetainScene(std::uint16_t index) {
    if (index>=AssetCount || Cached[index].SceneReferences==UINT32_MAX) he_cpp_custom::Fail("Disc scene cache reference overflow");
    if (Cached[index].SceneReferences==0 && Cached[index].StreamReferences==0) {
        if (Assets[index].ByteLength>CacheBudget-ReservedBytes) he_cpp_custom::Fail("Disc cache reservation exceeds budget");
        ReservedBytes+=Assets[index].ByteLength;
    }
    ++Cached[index].SceneReferences;
}
void Ps1DiscContent::ReleaseScene(std::uint16_t index) {
    if (index>=AssetCount || Cached[index].SceneReferences==0) he_cpp_custom::Fail("Disc scene cache reference underflow");
    --Cached[index].SceneReferences;DropUnused(index);
}
void Ps1DiscContent::ReleaseStream(std::uint16_t index) {
    if (index>=AssetCount || Cached[index].StreamReferences==0) he_cpp_custom::Fail("Disc stream cache reference underflow");
    --Cached[index].StreamReferences;DropUnused(index);
}
void Ps1DiscContent::DropUnused(std::uint16_t index) {
    auto& item=Cached[index];
    if (item.SceneReferences!=0 || item.StreamReferences!=0 || item.LoadingOwner!=nullptr) return;
    if (item.Bytes!=nullptr) { delete[] item.Bytes;item.Bytes=nullptr;CacheBytes-=Assets[index].ByteLength; }
    item.Ready=false;ReservedBytes-=Assets[index].ByteLength;
}
void Ps1DiscContent::Cancel(const void* owner) {
    Reader.Cancel(owner);
    if (ArchiveOwner==owner) ArchiveOwner=nullptr;
    for (std::uint32_t i=0;i<AssetCount;++i) if (Cached[i].LoadingOwner==owner) {
        auto& item=Cached[i];delete[] item.Bytes;item.Bytes=nullptr;item.LoadingOwner=nullptr;item.Ready=false;CacheBytes-=Assets[i].ByteLength;
    }
}
::Stream* Ps1DiscContent::OpenRead(HeCppString path) {
    for (std::uint32_t i=0;i<AssetCount;++i) if (path==Assets[i].RelativePath) {
        auto& item=Cached[i];
        if (!item.Ready || item.SceneReferences==0 || item.StreamReferences==UINT32_MAX)
            he_cpp_custom::Fail("Content OpenRead requires a completed active scene preload lease");
        ++item.StreamReferences;return new PinnedStream(*this,static_cast<std::uint16_t>(i),item.Bytes,Assets[i].ByteLength);
    }
    he_cpp_custom::Fail("Requested content path was not declared in packaged scene dependencies");
}

}  // namespace helengine::ps1
