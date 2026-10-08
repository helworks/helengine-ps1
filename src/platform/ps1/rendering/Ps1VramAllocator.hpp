#pragma once
#include <cstdint>
namespace helengine::ps1 {
// Fixed reservations exclude display buffers. Freed resources never move live resources.
class Ps1VramAllocator {
public:
 static constexpr std::uint16_t FirstFreeColumn=320, VramWidth=1024, VramHeight=512;
 static constexpr std::uint16_t ClutXAlignment=16, TexturePageWidthWords=64, Capacity=64;
 struct Region { std::uint16_t x=0,y=0,width=0,height=0; };
 void Reset() { for(auto& r:Regions) r={}; }
 bool TryAllocate(std::uint16_t w,std::uint16_t h,std::uint16_t& x,std::uint16_t& y) { return Allocate(w,h,1,false,x,y); }
 bool TryAllocateAligned(std::uint16_t w,std::uint16_t h,std::uint16_t a,std::uint16_t& x,std::uint16_t& y) { return Allocate(w,h,a,false,x,y); }
 bool TryAllocateWithinHorizontalPage(std::uint16_t w,std::uint16_t h,std::uint16_t& x,std::uint16_t& y) {
  if(w>64||h>256) return false;
  return Allocate(w,h,1,true,x,y);
 }
 bool Release(std::uint16_t x,std::uint16_t y,std::uint16_t w,std::uint16_t h) {
  for(auto& r:Regions) if(r.width&&r.x==x&&r.y==y&&r.width==w&&r.height==h) { r={};return true; }
  return false;
 }
 std::uint32_t UsedWords() const { std::uint32_t n=0;for(const auto& r:Regions)n+=std::uint32_t(r.width)*r.height;return n; }
 std::uint16_t LiveCount() const { std::uint16_t n=0;for(const auto& r:Regions)if(r.width)++n;return n; }
 const Region& GetRegion(std::uint16_t i) const { return Regions[i]; }
private:
 static std::uint32_t Align(std::uint32_t v,std::uint16_t a) { return (v+a-1u)/a*a; }
 bool Fits(std::uint32_t x,std::uint32_t y,std::uint16_t w,std::uint16_t h) const {
  if(x+w>VramWidth||y+h>VramHeight) return false;
  for(const auto& r:Regions) if(r.width&&x<r.x+r.width&&r.x<x+w&&y<r.y+r.height&&r.y<y+h)return false;
  return true;
 }
 bool Allocate(std::uint16_t w,std::uint16_t h,std::uint16_t a,bool page,std::uint16_t& x,std::uint16_t& y) {
  if(!w||!h||!a||w>VramWidth-FirstFreeColumn||h>VramHeight)return false;
  Region* slot=nullptr;for(auto& r:Regions)if(!r.width){slot=&r;break;}if(!slot)return false;
  std::uint32_t bestX=VramWidth,bestY=VramHeight;
  for(std::uint16_t row=0;row<Capacity+2;++row) {
   if(row>=2&&!Regions[row-2].width)continue;
   std::uint32_t cy=row==0?0:row==1?256:Regions[row-2].y+Regions[row-2].height;
   if(page&&cy%256+h>256)cy=Align(cy,256);
   for(std::uint16_t col=0;col<Capacity+1;++col) {
    if(col&&!Regions[col-1].width)continue;
    std::uint32_t cx=Align(col==0?FirstFreeColumn:Regions[col-1].x+Regions[col-1].width,a);
    if(page&&cx%64+w>64)cx=Align(cx,64);
    if((cy<bestY||(cy==bestY&&cx<bestX))&&Fits(cx,cy,w,h)){bestX=cx;bestY=cy;}
   }
  }
  if(bestY==VramHeight)return false;
  *slot={static_cast<std::uint16_t>(bestX),static_cast<std::uint16_t>(bestY),w,h};x=slot->x;y=slot->y;return true;
 }
 Region Regions[Capacity]{};
};
} // namespace helengine::ps1
