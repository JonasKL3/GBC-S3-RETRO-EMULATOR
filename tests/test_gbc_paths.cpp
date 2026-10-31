#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/GBC_S3/gbc_paths.h"
int main() {
  assert(strcmp(GbcS3Paths::ROOT, "/JOGOS/GBC") == 0);
  assert(strcmp(GbcS3Paths::ROMS, "/JOGOS/GBC/jogos") == 0);
  assert(strcmp(GbcS3Paths::COVER, "/JOGOS/GBC/cover") == 0);
  assert(strcmp(GbcS3Paths::SAVE, "/JOGOS/GBC/save") == 0);
  assert(GbcS3Paths::isRomPath("/JOGOS/GBC/jogos/Pokemon.gbc"));
  assert(GbcS3Paths::isRomPath("Tetris.gb"));
  assert(GbcS3Paths::isRomPath("/JOGOS/GBC/jogos/TEST.GBC"));
  assert(!GbcS3Paths::isRomPath("readme.txt"));
  char stem[64];
  assert(GbcS3Paths::makeRomStem(stem, sizeof(stem), "/JOGOS/GBC/jogos/Pokemon.gbc"));
  assert(strcmp(stem, "Pokemon") == 0);
  assert(GbcS3Paths::makeRomStem(stem, sizeof(stem), "Tetris.gb"));
  assert(strcmp(stem, "Tetris") == 0);
  char p[256];
  assert(GbcS3Paths::makeCoverPath(p, sizeof(p), GbcS3Paths::COVER, stem));
  assert(strcmp(p, "/JOGOS/GBC/cover/Tetris.cov") == 0);
  assert(!GbcS3Paths::makeCoverPath(p, sizeof(p), GbcS3Paths::COVER, "../escape"));
  assert(GbcS3Paths::makeStatePath(p, sizeof(p), 0x1234, 0xABCD, 0));
  assert(strstr(p, "/JOGOS/GBC/save/") == p);
  assert(strstr(p, ".ss0") != nullptr);
  assert(!GbcS3Paths::makeStatePath(p, sizeof(p), 1, 2, 4));
  assert(!GbcS3Paths::makeSramPath(p, 4, 0x1234, 0x5678));
  assert(GbcS3Paths::makeSramPath(p, sizeof(p), 0x1234, 0x5678));
  assert(strstr(p, "/JOGOS/GBC/save/") == p);
  assert(GbcS3Paths::makeRtcPath(p, sizeof(p), 0x1234, 0x5678));
  assert(strstr(p, ".rtc") != nullptr);
  assert(GbcS3Paths::makeVfsPath(p, sizeof(p), "/JOGOS/GBC/jogos/Game.gbc"));
  assert(strcmp(p, "/sd/JOGOS/GBC/jogos/Game.gbc") == 0);
  assert(!GbcS3Paths::makeVfsPath(p, sizeof(p), "relative.gbc"));
  puts("GBC path tests: PASS");
}
