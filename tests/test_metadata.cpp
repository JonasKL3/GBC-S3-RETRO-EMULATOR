#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <Arduino.h>
#include <SD.h>
#include "firmware/GBC_S3/halo_library.h"
SerialClass Serial;SDClass SD;
static std::vector<unsigned char> mainFile(){
  FILE *f=fopen(HALO_LIB_ROOT "/HALO_LIB.DAT","rb");assert(f);
  std::vector<unsigned char> data;int c;while((c=fgetc(f))!=EOF)data.push_back(c);assert(!ferror(f));fclose(f);return data;
}
int main(){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  assert(!HaloLibrary::load());
  const uint32_t id=HaloLibrary::gameId("/JOGOS/GBC/jogos/test.gbc");
  HaloLibrary::toggleFavorite(id);HaloLibrary::opened(id);HaloLibrary::setVolume(27);
  assert(HaloLibrary::load());assert(HaloLibrary::isFavorite(id));
  assert(HaloLibrary::lastGame()==id&&HaloLibrary::volume()==27);
  const auto original=mainFile();
  // Deliberately make the staging filename a directory: a failed write must
  // leave the previously committed metadata byte-for-byte intact.
  assert(SD.mkdir("/JOGOS/GBC/save/HALO_LIB.TMP"));
  assert(!HaloLibrary::save());assert(mainFile()==original);
  assert(HaloLibrary::load());assert(HaloLibrary::isFavorite(id));
  puts("Metadata PASS: flush/fsync/close commit, favorites/recent/volume roundtrip, write failure preserves committed file");
}
