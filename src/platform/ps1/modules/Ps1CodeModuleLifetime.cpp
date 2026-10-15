#include "platform/ps1/modules/Ps1CodeModuleLifetime.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"

namespace helengine::ps1 {
namespace {
Ps1CodeModulePinState* States=nullptr;
std::size_t Count=0;
Ps1CodeModulePinState& Find(const char* id) {
    if (id==nullptr || id[0]==0) he_cpp_custom::Fail("Code module pin id is empty");
    for (std::size_t i=0;i<Count;++i) if (__builtin_strcmp(States[i].ModuleId,id)==0) return States[i];
    he_cpp_custom::Fail("Code module pin id is not declared by the resident build");
}
}

void InstallCodeModulePinTable(Ps1CodeModulePinState* states,std::size_t count) {
    if (States!=nullptr || (count!=0 && states==nullptr)) he_cpp_custom::Fail("Code module pin table installed twice or missing");
    for (std::size_t i=0;i<count;++i) {
        if (states[i].ModuleId==nullptr || states[i].ModuleId[0]==0 || states[i].LiveObjects!=0 || states[i].Executable)
            he_cpp_custom::Fail("Initial code module pin state is invalid");
        for (std::size_t j=0;j<i;++j) if (__builtin_strcmp(states[i].ModuleId,states[j].ModuleId)==0)
            he_cpp_custom::Fail("Duplicate code module pin id");
    }
    States=states;Count=count;
}

void SetCodeModuleExecutable(const char* id,bool executable) {
    auto& state=Find(id);
    if (state.Executable==executable || (!executable && state.LiveObjects!=0))
        he_cpp_custom::Fail("Code module cannot be retired while objects remain alive");
    state.Executable=executable;
}

std::uint32_t GetCodeModuleLiveObjects(const char* id) { return Find(id).LiveObjects; }

void RetainCodeModule(const char* id) {
    if (id!=nullptr && id[0]==0) return;
    auto& state=Find(id);
    if (!state.Executable || state.LiveObjects==UINT32_MAX) he_cpp_custom::Fail("Code module retain outside lifetime or counter overflow");
    ++state.LiveObjects;
}

void ReleaseCodeModule(const char* id) {
    if (id!=nullptr && id[0]==0) return;
    auto& state=Find(id);
    if (!state.Executable || state.LiveObjects==0) he_cpp_custom::Fail("Code module release outside lifetime or counter underflow");
    --state.LiveObjects;
}

}  // namespace helengine::ps1

void HeCppRetainCodeModule(const char* id) { helengine::ps1::RetainCodeModule(id); }
void HeCppReleaseCodeModule(const char* id) { helengine::ps1::ReleaseCodeModule(id); }
