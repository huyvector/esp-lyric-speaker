#include "request_policy.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace vinyl;
  assert(!lyricRoute(false,0,0).searchFirst);
  assert(lyricRoute(true,0,0).searchFirst);
  assert(lyricRoute(false,5000,0).searchFirst);
  assert(!lyricRoute(true,0,5000).searchFirst);
  assert(lyricRoute(false,6000,2000).wait==2000);
  RequestGate fast;
  fast.failed(Service::LyricsGet,503,1,1000);assert(fast.remaining(Service::LyricsGet,1000)==2000);
  fast.failed(Service::LyricsGet,503,1,3000);assert(fast.remaining(Service::LyricsGet,3000)==4000);
  fast.failed(Service::LyricsGet,503,60,7000);assert(fast.remaining(Service::LyricsGet,7000)==60000);
  fast.succeeded(Service::LyricsGet);assert(!fast.remaining(Service::LyricsGet,7000));
  fast.failed(Service::LyricsGet,503,1,8000);assert(fast.remaining(Service::LyricsGet,8000)==2000);
  fast.failed(Service::LyricsGet,429,1,10000);assert(fast.remaining(Service::LyricsSearch,10000)==1000);
  RequestGate gate;
  gate.failed(Service::LyricsSearch, 503, 0, 1000);
  assert(gate.remaining(Service::LyricsSearch, 1000) == 30000);
  assert(gate.remaining(Service::LyricsGet, 1000) == 0); // Next song still gets an exact lookup.
  assert(gate.remaining(Service::Catalog, 1000) == 0);
  assert(gate.remaining(Service::Cover, 1000) == 0);
  assert(gate.remaining(Service::LyricsSearch, 31000) == 0);
  gate.failed(Service::LyricsGet, 429, 60, 40000);
  assert(gate.remaining(Service::LyricsSearch, 50000) == 50000);
  assert(gate.remaining(Service::LyricsGet, 50000) == 50000);
  assert(gate.remaining(Service::Catalog, 50000) == 0);
  gate.failed(Service::LyricsSearch, 404, 0, 50000);
  assert(gate.remaining(Service::LyricsGet, 50000) == 50000);
  RequestGate wrap;
  wrap.failed(Service::Catalog, 503, 45, 0xFFFFF000u);
  assert(wrap.remaining(Service::Catalog, 0xFFFFF000u + 2000u) == 43000);
  assert(wrap.remaining(Service::Catalog, 0xFFFFF000u + 45000u) == 0);
  wrap.failed(Service::Cover, 429, 0xFFFFFFFFu, 10);
  assert(wrap.remaining(Service::Cover, 10) == 86400000);
  assert(advertisement("Advertisement"));
  assert(advertisement("Advertisement • 1 of 1"));
  assert(advertisement(" advertisement\t"));
  assert(!advertisement("AdvertisementBand"));
  assert(!advertisement("")); assert(!advertisement("Rhymastic"));
  std::cout << "Endpoint 503 isolation / host 429 / Retry-After / clock wrap / ad metadata: PASS\n";
}
