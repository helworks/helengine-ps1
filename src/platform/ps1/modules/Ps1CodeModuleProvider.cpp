#include "platform/ps1/modules/Ps1CodeModuleProvider.hpp"
#include "IRuntimeCodeModule.hpp"
#include "helcpp_config.hpp"
#include "IRuntimeCodeModuleLoadOperation.hpp"
#include "RuntimeCodeModuleManifestEntry.hpp"
#include "RuntimeCodeModuleLoadState.hpp"
#include "platform/ps1/modules/Ps1ModuleTransfer.hpp"
#include "platform/ps1/modules/Ps1CodeModuleLifetime.hpp"
#include "platform/ps1/modules/Ps1ModuleGpuFence.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "psyqo/kernel.hh"

namespace helengine::ps1 {
namespace {
using Registration=void(*)(::Core*,::RuntimeComponentRegistry*);
using Initializer=void(*)();

class LoadedModule final : public ::IRuntimeCodeModule {
public:
    LoadedModule(Ps1CodeModuleProvider& provider,const Ps1DiscModuleBinding& binding,const Ps1ModulePackage& package)
        : Provider(provider),Binding(binding),Package(package) {}
    ~LoadedModule() override { Dispose(); }
    void Register(::Core* core,::RuntimeComponentRegistry* registry) override {
        if (Disposed || Registered || core==nullptr || registry==nullptr) he_cpp_custom::Fail("Invalid native module registration");
        Registered=true;
        reinterpret_cast<Registration>(Binding.Descriptor->LoadAddress+Package.RegisterOffset)(core,registry);
    }
    void Unregister(::Core* core,::RuntimeComponentRegistry* registry) override {
        if (Disposed || !Registered) he_cpp_custom::Fail("Invalid native module unregistration");
        reinterpret_cast<Registration>(Binding.Descriptor->LoadAddress+Package.UnregisterOffset)(core,registry);
        Registered=false;
    }
    void Dispose() override {
        if (Disposed) return;
        if (Registered) he_cpp_custom::Fail("Native module disposed before callback and factory removal");
        Provider.RequireGpuIdle();
        const auto* descriptor=Binding.Descriptor;
        auto* arena=reinterpret_cast<std::uint8_t*>(descriptor->LoadAddress);
        // Finalizers execute while their code and all resident pin hooks remain valid.
        for (std::uint32_t i=Package.FiniArrayCount;i!=0;--i) {
            const std::uint32_t address=Ps1ModulePackage::ReadU32(arena+Package.FiniArrayOffset+(i-1)*4);
            reinterpret_cast<Initializer>(address)();
        }
        if (GetCodeModuleLiveObjects(descriptor->ModuleId)!=0) he_cpp_custom::Fail("Native module still owns live objects after finalization");
        // Finalizers may release renderer resources; they must not leave new GPU references behind.
        Provider.RequireGpuIdle();
        SetCodeModuleExecutable(descriptor->ModuleId,false);
        __builtin_memset(arena,0,Package.BssOffset+Package.BssBytes);
        psyqo::Kernel::flushCache();
        Provider.ReleaseSlot(Binding.Slot);Disposed=true;
    }
private:
    Ps1CodeModuleProvider& Provider;
    const Ps1DiscModuleBinding& Binding;
    Ps1ModulePackage Package;
    bool Registered=false;
    bool Disposed=false;
};

class ModuleLoad final : public ::IRuntimeCodeModuleLoadOperation {
public:
    ModuleLoad(Ps1CodeModuleProvider& provider,Ps1DiscSectorReader& reader,Ps1DiscFileLocator& locator,
               const Ps1DiscModuleBinding& binding)
        : Provider(provider),Reader(reader),Locator(locator),Binding(binding) {}
    ~ModuleLoad() override { Dispose(); }
    bool get_IsCompleted() override { return Ready; }
    void Advance() override {
        if (Disposed || Ready) return;
        if (Transfer==nullptr) {
            if (!LookupStarted) { LookupStarted=Locator.BeginFind(this,Binding.Descriptor->DiscPath);return; }
            Ps1DiscFileRange file;bool failed=false;
            if (!Locator.PollFind(this,file,failed)) return;
            Reader.Consume(this);
            if (failed || file.ByteLength!=Binding.Descriptor->FileBytes) he_cpp_custom::Fail("Native module disc file is missing or has wrong length");
            Transfer=new Ps1ModuleTransfer(Reader,*Binding.Descriptor,file.FirstSector,
                reinterpret_cast<std::uint8_t*>(Binding.Descriptor->LoadAddress));
            return;
        }
        Transfer->Advance();
        if (Transfer->GetPhase()==Ps1ModuleTransfer::Phase::Failed) he_cpp_custom::Fail("Native module header/hash/range validation failed");
        if (Transfer->GetPhase()!=Ps1ModuleTransfer::Phase::Complete) return;
        const auto& package=Transfer->GetPackage();
        const auto& descriptor=*Binding.Descriptor;
        auto* arena=reinterpret_cast<std::uint8_t*>(descriptor.LoadAddress);
        if (Validated<package.InitArrayCount+package.FiniArrayCount) {
            const bool init=Validated<package.InitArrayCount;
            const std::uint32_t offset=init?package.InitArrayOffset+Validated*4:
                package.FiniArrayOffset+(Validated-package.InitArrayCount)*4;
            if (!package.FunctionValid(Ps1ModulePackage::ReadU32(arena+offset),descriptor)) he_cpp_custom::Fail("Native initializer points outside its module");
            ++Validated;return;
        }
        psyqo::Kernel::flushCache();
        SetCodeModuleExecutable(descriptor.ModuleId,true);
        // Initialization is one atomic publication prerequisite. Individual authored static callbacks
        // cannot be interrupted safely; cancellation is observed before this indivisible step.
        for (std::uint32_t i=0;i<package.InitArrayCount;++i)
            reinterpret_cast<Initializer>(Ps1ModulePackage::ReadU32(arena+package.InitArrayOffset+i*4))();
        Module=new LoadedModule(Provider,Binding,package);Ready=true;
    }
    ::IRuntimeCodeModule* TakeModule() override {
        if (!Ready || Module==nullptr || Disposed) he_cpp_custom::Fail("Native module result transferred before ready or twice");
        auto* result=Module;Module=nullptr;Transferred=true;return result;
    }
    void Dispose() override {
        if (Disposed) return;
        Reader.Cancel(this);
        delete Transfer;Transfer=nullptr;
        if (Module!=nullptr) { Module->Dispose();delete Module;Module=nullptr; }
        else if (!Transferred) Provider.ReleaseSlot(Binding.Slot);
        Disposed=true;
    }
private:
    Ps1CodeModuleProvider& Provider;
    Ps1DiscSectorReader& Reader;
    Ps1DiscFileLocator& Locator;
    const Ps1DiscModuleBinding& Binding;
    Ps1ModuleTransfer* Transfer=nullptr;
    LoadedModule* Module=nullptr;
    std::uint32_t Validated=0;
    bool LookupStarted=false;
    bool Ready=false;
    bool Transferred=false;
    bool Disposed=false;
};
}

Ps1CodeModuleProvider::Ps1CodeModuleProvider(Ps1DiscSectorReader& reader,Ps1DiscFileLocator& locator,psyqo::GPU& gpu,
    const Ps1DiscModuleBinding* bindings,std::size_t count) : Reader(reader),Locator(locator),Gpu(gpu),Bindings(bindings),Count(count) {
    if (count!=0 && bindings==nullptr) he_cpp_custom::Fail("Native module binding table missing");
    for (std::size_t i=0;i<count;++i) {
        if (bindings[i].Descriptor==nullptr || bindings[i].Slot>=8) he_cpp_custom::Fail("Native module slot declaration invalid");
        const auto& a=*bindings[i].Descriptor;
        constexpr std::uint32_t numericPolicy=HE_CPP_REAL_REPRESENTATION_VERSION |
            (HE_CPP_REAL_STORAGE_BITS << 8) | (HE_CPP_REAL_ARITHMETIC_BITS << 16);
        if (a.NumericPolicy!=numericPolicy) he_cpp_custom::Fail("Native module numeric policy differs from resident Core");
        if (a.ModuleId==nullptr || a.ModuleId[0]==0 || a.DiscPath==nullptr || a.LoadAddress<0x80010000u ||
            a.LoadAddress>=0x801f0000u || a.ArenaBytes==0 || a.ArenaBytes>0x801f0000u-a.LoadAddress)
            he_cpp_custom::Fail("Native module descriptor violates retail arena bounds");
        for (std::size_t j=0;j<i;++j) {
            const auto& b=*bindings[j].Descriptor;
            if (__builtin_strcmp(a.ModuleId,b.ModuleId)==0) he_cpp_custom::Fail("Duplicate native module id");
            if (bindings[i].Slot!=bindings[j].Slot && a.LoadAddress<b.LoadAddress+b.ArenaBytes && b.LoadAddress<a.LoadAddress+a.ArenaBytes)
                he_cpp_custom::Fail("Simultaneous native module arenas overlap");
            if (bindings[i].Slot==bindings[j].Slot && (a.LoadAddress!=b.LoadAddress || a.ArenaBytes!=b.ArenaBytes))
                he_cpp_custom::Fail("Native modules disagree on their shared slot bounds");
        }
    }
}

void Ps1CodeModuleProvider::RequireGpuIdle() {
    if (!IsModuleGpuIdle(Gpu)) he_cpp_custom::Fail("Native module retirement requires an idle GPU boundary");
}

::IRuntimeCodeModuleLoadOperation* Ps1CodeModuleProvider::BeginLoad(::RuntimeCodeModuleManifestEntry* entry) {
    if (entry==nullptr) he_cpp_custom::Fail("Native module entry missing");
    if (entry->get_RuntimeSpecializationId()!="ps1-overlay-v1" ||
        entry->get_LoadState()!=::RuntimeCodeModuleLoadState::Unloadable)
        he_cpp_custom::Fail("Native module entry does not describe a supported PS1 disc load unit");
    for (std::size_t i=0;i<Count;++i) if (entry->get_ModuleId()==Bindings[i].Descriptor->ModuleId) {
        const auto slot=Bindings[i].Slot;
        if (Slots[slot]) he_cpp_custom::Fail("Native code module arena is already pinned by another load unit");
        Slots[slot]=true;return new ModuleLoad(*this,Reader,Locator,Bindings[i]);
    }
    he_cpp_custom::Fail("Requested native code module has no packaged binding");
}

void Ps1CodeModuleProvider::ReleaseSlot(std::uint32_t slot) {
    if (slot>=8 || !Slots[slot]) he_cpp_custom::Fail("Native module slot release underflow");
    Slots[slot]=false;
}

}  // namespace helengine::ps1
