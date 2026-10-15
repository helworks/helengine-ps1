#ifndef MODULE_EXTRA
#error Actual overlay fixture requires its distinct compile-time identity
#endif
extern "C" void ResidentNote(int delta);
extern "C" int ResidentScale(int value);
namespace {
struct Lifetime {
    Lifetime() { ResidentNote(MODULE_EXTRA); }
    ~Lifetime() { ResidentNote(-MODULE_EXTRA); }
};
Lifetime GlobalLifetime;
int ZeroInitialized;
}
extern "C" void he_ps1_module_register(void*,void*) {
    ZeroInitialized = ResidentScale(MODULE_EXTRA);
    ResidentNote(ZeroInitialized);
}
extern "C" void he_ps1_module_unregister(void*,void*) { ResidentNote(-ZeroInitialized); }
