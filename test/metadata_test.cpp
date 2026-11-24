#include "metadata_policy.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  char a[]="Bài Ka Tuổi Trẻ (feat. Vũ Bùi Thu Thuỷ, Linh Cáo & Mel G)";
  vinyl::cleanGuestCredits(a); assert(!strcmp(a,"Bài Ka Tuổi Trẻ"));
  char b[]="Song (Live) [FEAT. Guest (Band)] (Acoustic Remix)";
  vinyl::cleanGuestCredits(b); assert(!strcmp(b,"Song (Live) (Acoustic Remix)"));
  char c[]="Song (ft. Guest"; vinyl::cleanGuestCredits(c); assert(!strcmp(c,"Song (ft. Guest"));
  char d[]="The (feather) Song"; vinyl::cleanGuestCredits(d); assert(!strcmp(d,"The (feather) Song"));
  assert(vinyl::primaryArtistLength("Emcee L (Da LAB), JGKiD (Da LAB)")==16);
  assert(vinyl::primaryArtistLength("Tiên Tiên")==strlen("Tiên Tiên"));
  assert(vinyl::primaryArtistLength("Band ; Other")==4);
  std::cout<<"Guest credits / nested brackets / preserve versions / artist query: PASS\n";
}
