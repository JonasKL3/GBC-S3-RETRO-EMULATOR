#include "gbc_savestates.h"
#include "gbc_paths.h"
#include "gnuboy_adapter.h"
#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
extern "C" {
#include "src/gnuboy/save.h"
#include "src/gnuboy/hw.h"
#include "src/gnuboy/mem.h"
#include "src/gnuboy/lcd.h"
#include "src/gnuboy/sound.h"
#include "src/gnuboy/rtc.h"
}

namespace {
static constexpr uint16_t STATE_VERSION = 1;
static constexpr uint8_t SLOTS[] = {0, 1, 2, 3, 9};
static constexpr size_t IO_CHUNK = 2048;
static constexpr char MAGIC[8] = {'G','B','C','S','3','S','T','1'};
// Reused non-reentrant scratch buffers keep the Arduino loop task stack small.
// State operations run synchronously from the frontend task only.
static uint8_t stateIoBuffer[IO_CHUNK];
static uint8_t stateHeaderBuffer[4096];
static constexpr char PAYLOAD_TMP[] = "/JOGOS/GBC/save/.gbc_state_payload.tmp";
static constexpr char LOAD_TMP[] = "/JOGOS/GBC/save/.gbc_state_load.tmp";
#pragma pack(push, 1)
struct StateHeader {
  char magic[8];
  uint16_t version;
  uint16_t headerSize;
  uint8_t slot;
  uint8_t reserved[3];
  uint32_t pathHash;
  uint32_t romHash;
  uint32_t romSize;
  uint32_t payloadSize;
  uint32_t payloadCrc;
  uint32_t headerCrc;
};
#pragma pack(pop)
static uint32_t currentPathHash = 0, currentRomHash = 0, currentRomSize = 0;
static char key[40] = {};
// Periodic SD writes can stall gameplay for seconds on a slow/shared card.
// Keep manual saves available; the pause menu can explicitly enable autosave.
static bool haveGame = false, autoEnabled = false;
static uint32_t lastAutoAt = 0;
static bool slotKnown[10] = {}, slotExists[10] = {};

static bool validSlot(int slot) { return (slot >= 0 && slot <= 3) || slot == 9; }
static bool knownSlot(uint8_t slot) { for (uint8_t s : SLOTS) if (s == slot) return true; return false; }
static uint32_t fnv1a(const uint8_t *data, size_t size, uint32_t h = 2166136261UL) {
  for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 16777619UL;
  return h;
}
static uint32_t pathHashOf(const char *path) {
  return path ? fnv1a(reinterpret_cast<const uint8_t *>(path), strlen(path)) : 0;
}
static uint32_t crcUpdate(uint32_t crc, const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1U));
  }
  return crc;
}
static uint32_t crc32(const uint8_t *data, size_t size) { return crcUpdate(0xFFFFFFFFUL, data, size) ^ 0xFFFFFFFFUL; }
static uint32_t expectedPayloadSize() {
  const uint32_t irl = hw.cgb ? 8U : 2U;
  const uint32_t vrl = hw.cgb ? 4U : 2U;
  const uint32_t srl = (uint32_t)mbc.ramsize << 1;
  return 4096U * (1U + irl + vrl + srl);
}
static bool makePath(char *out, size_t cap, int slot, const char *suffix) {
  return haveGame && GbcS3Paths::makeStatePath(out, cap, currentPathHash, currentRomHash, slot, suffix);
}
static bool toVfsPath(char *out, size_t cap, const char *sdPath) {
  return GbcS3Paths::makeVfsPath(out, cap, sdPath);
}
static bool readExact(File &f, uint8_t *dst, size_t size) {
  size_t done = 0;
  while (done < size) { size_t n = f.read(dst + done, size - done); if (!n) return false; done += n; delay(1); }
  return true;
}
static bool writeExact(File &f, const uint8_t *src, size_t size) {
  size_t done = 0;
  while (done < size) { size_t n = f.write(src + done, size - done); if (!n) return false; done += n; delay(1); }
  return true;
}
static bool validateRawHeader(const uint8_t *buf) { return gnuboy_state_header_valid(buf)!=0; }
static void fillHeader(StateHeader *h, int slot, uint32_t payloadSize, uint32_t payloadCrc) {
  memset(h, 0, sizeof(*h)); memcpy(h->magic, MAGIC, sizeof(MAGIC));
  h->version = STATE_VERSION; h->headerSize = sizeof(*h); h->slot = (uint8_t)slot;
  h->pathHash = currentPathHash; h->romHash = currentRomHash; h->romSize = currentRomSize;
  h->payloadSize = payloadSize; h->payloadCrc = payloadCrc;
  h->headerCrc = crc32(reinterpret_cast<const uint8_t *>(h), offsetof(StateHeader, headerCrc));
}
static bool payloadCrcFile(const char *vfsPath, uint32_t expected, uint32_t *crcOut) {
  FILE *f = fopen(vfsPath, "rb"); if (!f) return false;
  uint32_t crc = 0xFFFFFFFFUL; size_t total = 0; bool ok = true;
  for (;;) { size_t n = fread(stateIoBuffer, 1, sizeof(stateIoBuffer), f); if (n) { crc = crcUpdate(crc, stateIoBuffer, n); total += n; delay(1); }
    if (n < sizeof(stateIoBuffer)) { if (ferror(f)) ok = false; break; } }
  fclose(f); if (!ok || total != expected) return false;
  *crcOut = crc ^ 0xFFFFFFFFUL; return true;
}
static bool validFile(const char *path, int slot, bool verifyPayload) {
  File f = SD.open(path, FILE_READ); if (!f) return false;
  StateHeader h = {}; bool ok = f.size() >= sizeof(h) && readExact(f, reinterpret_cast<uint8_t *>(&h), sizeof(h));
  const uint32_t expected = expectedPayloadSize();
  if (ok) ok = memcmp(h.magic,MAGIC,sizeof(MAGIC))==0 && h.version==STATE_VERSION &&
      h.headerSize==sizeof(h) && h.slot==(uint8_t)slot && knownSlot(h.slot) &&
      h.pathHash==currentPathHash && h.romHash==currentRomHash && h.romSize==currentRomSize &&
      h.payloadSize==expected && h.headerCrc==crc32(reinterpret_cast<const uint8_t *>(&h),offsetof(StateHeader,headerCrc)) &&
      f.size()==sizeof(h)+(size_t)h.payloadSize;
  if (ok && verifyPayload) {
    ok = readExact(f, stateHeaderBuffer, sizeof(stateHeaderBuffer)) && validateRawHeader(stateHeaderBuffer);
    uint32_t crc = 0xFFFFFFFFUL; if (ok) crc = crcUpdate(crc, stateHeaderBuffer, sizeof(stateHeaderBuffer));
    size_t remaining = ok ? h.payloadSize - sizeof(stateHeaderBuffer) : 0;
    while (ok && remaining) { size_t want = remaining > sizeof(stateIoBuffer) ? sizeof(stateIoBuffer) : remaining; size_t n = f.read(stateIoBuffer, want); if (n != want) { ok = false; break; } crc = crcUpdate(crc,stateIoBuffer,n); remaining -= n; delay(1); }
    if (ok && (crc ^ 0xFFFFFFFFUL) != h.payloadCrc) ok = false;
  }
  f.close(); return ok;
}
static bool preserveInvalid(const char *path) {
  if (!SD.exists(path)) return true;
  char bad[180];
  for (unsigned i=0;i<10;++i) { snprintf(bad,sizeof(bad),"%s.bad%u",path,i); if (SD.exists(bad)) continue;
    if (SD.rename(path,bad)) { Serial.printf("[GBC][STATE][PRESERVE] %s\n",bad); return true; } return false; }
  return false;
}
static bool recoverMain(int slot, const char *path, const char *backup) {
  if (validFile(path,slot,true)) return true;
  if (validFile(backup,slot,true)) {
    if (!preserveInvalid(path)) return false;
    if (!SD.rename(backup,path)) return false;
    return validFile(path,slot,true);
  }
  // Keep a corrupt state as .badN rather than silently overwriting it.
  return preserveInvalid(path);
}
static bool copyPayloadToRawFile(const char *wrappedPath, const char *rawSdPath, int slot) {
  File src = SD.open(wrappedPath, FILE_READ); if (!src) return false;
  StateHeader h = {}; bool ok = readExact(src,reinterpret_cast<uint8_t *>(&h),sizeof(h));
  char vfs[320]; if (!toVfsPath(vfs,sizeof(vfs),rawSdPath)) ok = false;
  FILE *dst = ok ? fopen(vfs,"wb") : nullptr; if (!dst) ok = false;
  size_t remaining = ok ? h.payloadSize : 0;
  while (ok && remaining) { size_t want = remaining > sizeof(stateIoBuffer) ? sizeof(stateIoBuffer) : remaining; size_t n = src.read(stateIoBuffer,want); if (n != want || fwrite(stateIoBuffer,1,n,dst)!=n) { ok=false; break; } remaining-=n; delay(1); }
  if (dst) { if (fflush(dst)!=0) ok=false; fclose(dst); }
  src.close();
  if (ok) { uint32_t crc=0; ok=payloadCrcFile(vfs,h.payloadSize,&crc) && crc==h.payloadCrc; }
  if (ok) {
    FILE *check=fopen(vfs,"rb");
    if (!check || fread(stateHeaderBuffer,1,sizeof(stateHeaderBuffer),check)!=sizeof(stateHeaderBuffer) || !validateRawHeader(stateHeaderBuffer)) ok=false;
    if (check) fclose(check);
  }
  if (!ok) SD.remove(rawSdPath);
  (void)slot;
  return ok;
}
static bool applyStateFrom(const char *path, int slot) {
  if (!validFile(path,slot,true)) return false;
  if (SD.exists(LOAD_TMP) && !SD.remove(LOAD_TMP)) return false;
  char rawVfs[320]; if (!toVfsPath(rawVfs,sizeof(rawVfs),LOAD_TMP)) return false;
  if (!copyPayloadToRawFile(path,LOAD_TMP,slot)) return false;
  FILE *f=fopen(rawVfs,"rb"); if (!f) { SD.remove(LOAD_TMP); return false; }
  const bool restored=loadstate_checked(f)!=0;
  fclose(f); SD.remove(LOAD_TMP);
  if(!restored)return false;
  vram_dirty(); pal_dirty(); sound_dirty(); mem_updatemap();
  return true;
}
} // namespace

namespace GbcSaveState {
void selectGame(const char *romPath, uint32_t romHash, uint32_t romSize) {
  currentPathHash=pathHashOf(romPath); currentRomHash=romHash; currentRomSize=romSize;
  snprintf(key,sizeof(key),"%08lX_%08lX",(unsigned long)currentPathHash,(unsigned long)currentRomHash);
  haveGame=romPath&&romPath[0]&&romSize!=0; memset(slotKnown,0,sizeof(slotKnown)); memset(slotExists,0,sizeof(slotExists));
  lastAutoAt=millis(); Serial.printf("[GBC][STATE][GAME] key=%s dir=%s\n",key,GbcS3Paths::SAVE);
}
bool enabled(){return autoEnabled;}
void setEnabled(bool value){autoEnabled=value;lastAutoAt=millis();}
void resetAutoTimer(){lastAutoAt=millis();}
bool autoDue(){return haveGame&&autoEnabled&&(uint32_t)(millis()-lastAutoAt)>=AUTO_INTERVAL_MS;}
const char *gameKey(){return key;}
bool present(int slot){
  if(!haveGame||!validSlot(slot))return false;
  if(slotKnown[slot])return slotExists[slot];
  char path[128],bak[136]; bool ok=makePath(path,sizeof(path),slot,"")&&validFile(path,slot,true);
  if(!ok&&makePath(bak,sizeof(bak),slot,".bak"))ok=validFile(bak,slot,true);
  slotKnown[slot]=true;slotExists[slot]=ok;return ok;
}
bool save(int slot){
  if(!haveGame||!GbcCore::loaded()||!validSlot(slot))return false;
  if(!SD.exists(GbcS3Paths::SAVE)&&!SD.mkdir(GbcS3Paths::SAVE))return false;
  char path[128],tmp[136],bak[136],payloadVfs[320];
  if(!makePath(path,sizeof(path),slot,"")||!makePath(tmp,sizeof(tmp),slot,".tmp")||!makePath(bak,sizeof(bak),slot,".bak")||!toVfsPath(payloadVfs,sizeof(payloadVfs),PAYLOAD_TMP))return false;
  if(!recoverMain(slot,path,bak)){Serial.printf("[GBC][STATE][ABORT] slot=%d preservando arquivo anterior\n",slot);return false;}
  if(SD.exists(tmp)&&!SD.remove(tmp))return false;
  if(SD.exists(PAYLOAD_TMP)&&!SD.remove(PAYLOAD_TMP))return false;
  errno=0;FILE *payload=fopen(payloadVfs,"wb");
  if(!payload){Serial.printf("[GBC][STATE][FAIL] payload open errno=%d\n",errno);return false;}
  errno=0;const bool serialized=savestate_checked(payload)!=0;const int serializeErr=errno;
  errno=0;const int flushResult=fflush(payload);const int flushErr=errno;
  const int streamError=ferror(payload);
  errno=0;const long rawSize=ftell(payload);const int tellErr=errno;
  errno=0;const int closeResult=fclose(payload);const int closeErr=errno;
  bool ok=serialized&&flushResult==0&&!streamError&&rawSize>=0&&closeResult==0;
  const uint32_t expected=expectedPayloadSize();
  if(!ok||rawSize!=(long)expected){
    SD.remove(PAYLOAD_TMP);
    Serial.printf("[GBC][STATE][FAIL] payload size=%ld expected=%lu serialize=%d errno=%d ferror=%d flush=%d errno=%d tell_errno=%d close=%d errno=%d\n",
      rawSize,(unsigned long)expected,(int)serialized,serializeErr,streamError,flushResult,flushErr,tellErr,closeResult,closeErr);
    return false;
  }
  uint32_t payloadCrc=0;if(!payloadCrcFile(payloadVfs,expected,&payloadCrc)){SD.remove(PAYLOAD_TMP);return false;}
  // Validate GNUBoy's own internal offset table before wrapping it.
  FILE *rawCheck=fopen(payloadVfs,"rb");
  if(!rawCheck||fread(stateHeaderBuffer,1,sizeof(stateHeaderBuffer),rawCheck)!=sizeof(stateHeaderBuffer)||!validateRawHeader(stateHeaderBuffer))ok=false;
  if(rawCheck)fclose(rawCheck);
  if(!ok){SD.remove(PAYLOAD_TMP);Serial.println("[GBC][STATE][FAIL] cabecalho interno GNUBoy invalido");return false;}
  StateHeader header={};fillHeader(&header,slot,expected,payloadCrc);
  File out=SD.open(tmp,FILE_WRITE);ok=(bool)out;
  if(ok)ok=writeExact(out,reinterpret_cast<const uint8_t *>(&header),sizeof(header));
  FILE *src=fopen(payloadVfs,"rb");if(!src)ok=false;
  size_t remaining=expected;
  while(ok&&remaining){size_t want=remaining>sizeof(stateIoBuffer)?sizeof(stateIoBuffer):remaining;size_t n=fread(stateIoBuffer,1,want,src);if(n!=want||!writeExact(out,stateIoBuffer,n)){ok=false;break;}remaining-=n;}
  if(src)fclose(src);if(out){out.flush();out.close();}
  SD.remove(PAYLOAD_TMP);
  if(!ok||!validFile(tmp,slot,true)){if(SD.exists(tmp))SD.remove(tmp);Serial.println("[GBC][STATE][FAIL] temporario nao passou CRC/validacao");return false;}
  const bool hadMain=SD.exists(path);
  if(hadMain){if(SD.exists(bak)&&!SD.remove(bak))return false;if(!SD.rename(path,bak))return false;}
  if(!SD.rename(tmp,path)){if(hadMain&&!SD.exists(path)&&SD.exists(bak))SD.rename(bak,path);return false;}
  slotKnown[slot]=true;slotExists[slot]=true;lastAutoAt=millis();
  Serial.printf("[GBC][STATE][SAVE] slot=%d bytes=%lu path=%s\n",slot,(unsigned long)(sizeof(header)+expected),path);return true;
}
bool load(int slot){
  if(!haveGame||!GbcCore::loaded()||!validSlot(slot))return false;
  char path[128],bak[136];if(!makePath(path,sizeof(path),slot,"")||!makePath(bak,sizeof(bak),slot,".bak"))return false;
  if(applyStateFrom(path,slot)){slotKnown[slot]=true;slotExists[slot]=true;lastAutoAt=millis();Serial.printf("[GBC][STATE][LOAD] slot=%d OK\n",slot);return true;}
  if(applyStateFrom(bak,slot)){
    if(preserveInvalid(path))SD.rename(bak,path);
    slotKnown[slot]=true;slotExists[slot]=true;lastAutoAt=millis();Serial.printf("[GBC][STATE][LOAD] slot=%d backup recuperado\n",slot);return true;
  }
  slotKnown[slot]=true;slotExists[slot]=false;Serial.printf("[GBC][STATE][LOAD][FAIL] slot=%d ausente/invalido/ROM diferente\n",slot);return false;
}
int saveAuto(){if(!autoDue())return 0;lastAutoAt=millis();return save(9)?1:-1;}
} // namespace GbcSaveState
