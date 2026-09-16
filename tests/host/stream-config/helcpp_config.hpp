#pragma once
// Host test configuration for the embedded asset stream: standard storage, no unwinding,
// failures routed to the test-owned he_cpp_custom::Fail so the failure path is observable.
#define HE_CPP_USE_STD_STRING 1
#define HE_CPP_USE_STD_VECTOR 1
#define HE_CPP_USE_STD_UNORDERED_MAP 1
#define HE_CPP_USE_STD_UNORDERED_SET 1
#define HE_CPP_USE_EXCEPTIONS 0
#define HE_CPP_USE_RTTI 0
#define HE_CPP_COMPACT_NATIVE_EXCEPTION_MESSAGES 1
#define HE_CPP_RUNTIME_PROVIDER_HEADER "Ps1StreamTestProvider.hpp"
