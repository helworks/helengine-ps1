#pragma once

#include <cstdint>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/functional.h>
#include "platform/ps1/Ps1SharedPtr.hpp"
#include "platform/ps1/Ps1Hash.hpp"

namespace psyqo { class GPU; }

/// <summary>Supplies SDK storage and services through codegen's generic provider contract.</summary>
namespace he_cpp_custom {
using String = eastl::string;
template<class T> using Vector = eastl::vector<T>;
template<class K, class V, class H, class E>
using UnorderedMap = eastl::hash_map<K, V, H, E>;
template<class T, class H, class E>
using UnorderedSet = eastl::hash_set<T, H, E>;
template<class T> using Hash = helengine::ps1::Ps1Hash<T>;
template<class T> using Function = eastl::function<T>;
template<class T> using SharedPtr = helengine::ps1::Ps1SharedPtr<T>;

/// <summary>Stops execution after reporting an unrecoverable managed runtime failure.</summary>
[[noreturn]] void Fail(const char* message);
/// <summary>Returns the extended HSync monotonic timer in microseconds.</summary>
std::uint64_t MonotonicMicroseconds();
}

namespace helengine::ps1 {
/// <summary>Binds the initialized HSync counter before constructing generated Core objects.</summary>
void InitializeRuntimeClock(psyqo::GPU& gpu);
}

