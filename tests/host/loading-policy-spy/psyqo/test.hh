#pragma once
#include <cassert>
#include <cstdlib>
#undef assert
inline void assert(bool value) { if (!value) std::abort(); }
#include <cstdint>
#include <vector>
namespace psyqo {
union Color { struct { std::uint8_t r,g,b,pad; }; std::uint32_t packed; };
namespace Prim { struct Triangle {}; struct GouraudTriangle {}; struct GouraudTexturedTriangle {}; struct TexturedQuad {}; struct FastFill { Color ColorValue{}; }; }
namespace Fragments { template<class T> struct SimpleFragment { T primitive; }; }
template<unsigned N> struct OrderingTable {
    std::vector<const void*> Packets;
    void clear(){Packets.clear();}
    // PSYQo inserts at the head of each bucket; these fixtures use one bucket per camera.
    template<class T> void insert(T& value,int){Packets.insert(Packets.begin(), &value);}
};
namespace Kernel { inline void assert(bool value,const char*){::assert(value);} }
struct GPU {
    struct Call { bool Clear; const void* Address; std::uint32_t Color; unsigned Packets; std::vector<const void*> ExecutionOrder; };
    std::vector<Call> Calls;
    void getNextClear(Prim::FastFill& fill,Color color){fill.ColorValue=color;}
    void chain(Fragments::SimpleFragment<Prim::FastFill>& fill){Calls.push_back({true,&fill,fill.primitive.ColorValue.packed,0,{}});}
    template<unsigned N> void chain(OrderingTable<N>& table){Calls.push_back({false,&table,0,static_cast<unsigned>(table.Packets.size()),table.Packets});}
};
}
