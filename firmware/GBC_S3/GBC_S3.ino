/*
 * GBC-S3 — emulador de Game Boy / Game Boy Color para ESP32-S3.
 * Usa SD-FIRST/HSPI, TFT ILI9341, biblioteca local, capas NS3C RGB565 e Dabble BLE.
 * Núcleo GNUBoy GPL-2.0-or-later, framebuffer RGB565 e PCM -> I2S/MAX98357A.
 * Save states GNUBoy encapsulados/validados em /JOGOS/GBC/save/.
 */
#include <Arduino.h>
#ifdef SET_LOOP_TASK_STACK_SIZE
// GNUBoy + stdio/FatFS need more headroom than Arduino's default 8 KiB.
SET_LOOP_TASK_STACK_SIZE(24576);
#else
#error "Use Arduino-ESP32 with SET_LOOP_TASK_STACK_SIZE support (validated with 3.3.12)"
#endif
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include "halo_library.h"
#include "gbc_paths.h"
#include "gbc_video_scale.h"
#include "gbc_s3_covers.h"
#include "gnuboy_adapter.h"
#include "gbc_video_output.h"
#include "gbc_audio_output.h"
#include "gbc_savestates.h"

#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#ifdef GBC_HOST_TEST
#include <DabbleESP32.h>
#else
#include "src/dabble/DabbleESP32.h"
#endif


#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  #error "Selecione ESP32S3 Dev Module no Arduino IDE"
#endif
#if !defined(USE_HSPI_PORT)
  #error "No TFT_eSPI: habilite USE_HSPI_PORT no User_Setup ativo"
#endif
#if defined(TOUCH_CS) && (TOUCH_CS == 14)
  #error "Desative TOUCH_CS 14: GPIO14 pertence ao microSD"
#endif
#if !defined(TFT_CS) || (TFT_CS != 10) || !defined(TFT_DC) || (TFT_DC != 9) || !defined(TFT_RST) || (TFT_RST != 4)
  #error "TFT CS/DC/RST devem ser 10/9/4"
#endif
#if !defined(TFT_MOSI) || (TFT_MOSI != 11) || !defined(TFT_MISO) || (TFT_MISO != 13) || !defined(TFT_SCLK) || (TFT_SCLK != 12)
  #error "TFT MOSI/MISO/SCK devem ser 11/13/12"
#endif

#ifndef GBC_ENABLE_SRAM_SAVES
// A escrita de SRAM fica desabilitada ate ser validada na placa real com BLE ativo.
// Depois do teste SD+BLE, defina GBC_ENABLE_SRAM_SAVES=1 nesta linha.
#define GBC_ENABLE_SRAM_SAVES 0
#endif

namespace Config {
static constexpr uint8_t SD_CS = 14;
static constexpr uint32_t SD_HZ = 1000000;
static constexpr uint16_t MAX_GAMES = 500;
static constexpr uint8_t MENU_ROWS = 7;
static constexpr size_t PATH_CAP = 240;
static constexpr size_t NAME_CAP = 65;
static constexpr size_t ROM_LIMIT = 4UL * 1024UL * 1024UL;
static constexpr uint32_t FRAME_US = 16743;
static constexpr bool EXPERIMENTAL_SD_SAVES = (GBC_ENABLE_SRAM_SAVES != 0);
}

static constexpr uint8_t TFT_ROTATION = 2;
static constexpr uint8_t SD_BOOT_RETRIES = 3;
static constexpr uint8_t SD_RECOVERY_RETRIES = 2;
static constexpr uint16_t LCD_W = 160, LCD_H = 144;
static constexpr uint16_t SCREEN_W = 240, SCREEN_H = 216;
static constexpr uint16_t X0 = 40, Y0 = 12;
static constexpr uint16_t FAST_X0 = 80, FAST_Y0 = 48;
static constexpr bool panelBgrAlternate = true;
static constexpr bool imageSwapBytes = true;
#if defined(M5STACK)
  static constexpr uint8_t ILI9341_ROT2_MADCTL = 0x20 | 0x40;
#else
  static constexpr uint8_t ILI9341_ROT2_MADCTL = 0x80;
#endif
#if defined(TFT_RGB_ORDER) && (TFT_RGB_ORDER == 1)
  static constexpr uint8_t ILI9341_DEFAULT_COLOR_BIT = 0x00;
#else
  static constexpr uint8_t ILI9341_DEFAULT_COLOR_BIT = 0x08;
#endif

enum ButtonMask : uint16_t {
  BTN_A=1<<0, BTN_B=1<<1, BTN_SELECT=1<<2, BTN_START=1<<3,
  BTN_UP=1<<4, BTN_DOWN=1<<5, BTN_LEFT=1<<6, BTN_RIGHT=1<<7
};
enum VideoMode : uint8_t { AMPLIADO=0, RAPIDO=1 };

struct GbGame { char path[Config::PATH_CAP]; char title[Config::NAME_CAP]; };

static TFT_eSPI tft(320,240);
static GbGame *gameList=nullptr;
static uint16_t gameCount=0, menuSelection=0, menuTop=0;
static uint16_t visibleGames[Config::MAX_GAMES]={};
static uint16_t visibleCount=0, visibleSelection=0;
static uint8_t libraryView=0; // 0=todos, 1=favoritos, 2=recentes
static char librarySearch[13]={};
static bool keyboardOpen=false, resumePrompt=false;
static uint8_t keyboardCursor=0;
static bool sdOnline=false, sdCatalogVerified=false;
static uint32_t sdRecoveryCount=0, sdReadFailures=0;
static bool playing=false;
static int romIoError=0;
static bool gameWaitForRelease=false;
static uint16_t *coreFrame=nullptr, *rgbFrame=nullptr;
static bool coreFrameInternal=false;
static size_t romSize=0, cartRamSize=0;
static uint32_t activeRomHash=0;
static bool cartRamDirty=false, cartBatteryBacked=false;
static char activePath[Config::PATH_CAP]={};
static uint32_t lastMenuInputMs=0, exitHoldMs=0, frameCount=0;
static int64_t nextFrameUs=0;
static VideoMode videoMode=AMPLIADO;
static constexpr unsigned VIDEO_STRIPE_ROWS=8;
static bool paused=false, pauseNeedsRelease=false, pauseAwaitActionRelease=false;
static uint8_t pauseSelection=0;
static bool pauseSettings=false;
static uint8_t pauseSettingsSelection=0;
static constexpr uint8_t SAVE_SLOT_IDS[5]={0,1,2,3,9};
static uint8_t selectedSaveSlot=0;
static char pauseStatusText[72]={};
static bool pauseStatusOk=true;
static uint32_t pauseStatusUntil=0;
static bool leaveGameRequested=false;

static void *psAlloc(size_t n) {
  return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}

static void applyColorPipeline(const char *reason) {
  const uint8_t colorBit=(uint8_t)(ILI9341_DEFAULT_COLOR_BIT ^ (panelBgrAlternate ? 0x08 : 0));
  const uint8_t madctl=(uint8_t)(ILI9341_ROT2_MADCTL | colorBit);
  tft.writecommand(0x36);
  tft.writedata(madctl);
  tft.setSwapBytes(imageSwapBytes);
  Serial.printf("[COLOR][PIPE] %s MADCTL=0x%02X BGR_ALT=%u swap=%u rotation=%u\n",
    reason,madctl,(unsigned)panelBgrAlternate,(unsigned)imageSwapBytes,TFT_ROTATION);
}
static void reportMemory(const char *step) {
  Serial.printf("[MEM][%s] freeHeap=%u freePSRAM=%u rom=%u ram=%u fbInternal=%s\n",step,
    (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram(),(unsigned)romSize,(unsigned)cartRamSize,coreFrameInternal?"YES":"NO");
}
static void drawBoot() {
  tft.fillScreen(0x000B);
  tft.drawRoundRect(38,53,244,132,15,0x03FF);
  tft.drawRoundRect(41,56,238,126,12,0x023F);
  tft.fillRoundRect(59,83,202,60,10,0x000B);
  tft.setTextColor(TFT_CYAN,0x000B);tft.setTextSize(4);tft.setCursor(83,95);tft.print("GBC-S3");
  tft.drawFastHLine(80,157,160,0x03FF);
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,0x000B);tft.setCursor(80,166);tft.print("RETRO SYSTEM  ESP32-S3");
}
static void statusScreen(const char *line1,const char *line2="") {
  GbcVideoOutput::invalidate();
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);tft.setTextColor(TFT_CYAN,TFT_BLACK);tft.setCursor(10,16);tft.print("GBC-S3");
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,TFT_BLACK);tft.setCursor(12,78);tft.print(line1);
  tft.setCursor(12,100);tft.setTextColor(TFT_YELLOW,TFT_BLACK);tft.print(line2);
  Serial.printf("[STATUS] %s | %s\n",line1,line2);
}

static bool mountSD(uint8_t maxAttempts,const char *reason) {
  for(uint8_t a=1;a<=maxAttempts;a++) {
    digitalWrite(TFT_CS,HIGH);digitalWrite(Config::SD_CS,HIGH);
    bool ok=SD.begin(Config::SD_CS,tft.getSPIinstance(),Config::SD_HZ,"/sd",5,false);
    Serial.printf("[SD][%s] tentativa=%u/%u resultado=%s\n",reason,(unsigned)a,(unsigned)maxAttempts,ok?"OK":"FAIL");
    if(ok){sdOnline=true;return true;}
    delay(200);
  }
  sdOnline=false;return false;
}
static bool probeGbDirectory(const char *stage) {
  errno=0;
  File dir=SD.open(GbcS3Paths::ROMS,FILE_READ);
  const bool ok=(dir && dir.isDirectory());
  const int openErr=errno;
  if(dir)dir.close();
  Serial.printf("[SD][PROBE][%s] %s=%s errno=%d\n",stage,GbcS3Paths::ROMS,ok?"OK":"FAIL",openErr);
  return ok;
}
static bool recoverSDOnMenu(int triggeringErr) {
  if(playing){Serial.println("[SD][RECOVER][BLOCK] jogo ativo");return false;}
  ++sdReadFailures;
  Serial.printf("[SD][RECOVER] errno=%d inicio contador=%lu\n",triggeringErr,(unsigned long)sdReadFailures);
  for(uint8_t cycle=1;cycle<=SD_RECOVERY_RETRIES;cycle++) {
    GbcS3Covers::invalidate();
    SD.end();sdOnline=false;
    digitalWrite(TFT_CS,HIGH);digitalWrite(Config::SD_CS,HIGH);delay(150*cycle);
    if(!mountSD(1,"RECOVER"))continue;
    if(probeGbDirectory("REMOUNT")){++sdRecoveryCount;return true;}
  }
  sdOnline=false;Serial.println("[SD][RECOVER][FAIL] SD/GB continua inacessivel; confira CS/MISO/alimentacao/cartao");
  return false;
}
static bool ensureDirectory(const char *path) {
  if(SD.exists(path))return true;
  const bool made=SD.mkdir(path);
  const bool ok=made||SD.exists(path);
  Serial.printf("[SD][DIR] %s %s\n",path,ok?"OK":"FAIL");
  return ok;
}
static bool ensureGbDirectories() {
  const char *dirs[]={"/JOGOS",GbcS3Paths::ROOT,GbcS3Paths::CACHE_INDICES,GbcS3Paths::ORIGINAL_COVERS,GbcS3Paths::ROMS,GbcS3Paths::PNG,GbcS3Paths::COVER,GbcS3Paths::REPORTS,GbcS3Paths::SAVE};
  bool ok=true;
  for(const char *dir:dirs)if(!ensureDirectory(dir))ok=false;
  return ok;
}

static uint16_t readDabbleButtons() {
#ifndef GBC_HOST_TEST
  if(!esp32ble.connected())return 0;
#endif

  uint16_t m=0;
  const bool left=GamePad.isLeftPressed(),right=GamePad.isRightPressed();
  const bool up=GamePad.isUpPressed(),down=GamePad.isDownPressed();
  const int x=GamePad.getXaxisData(),y=GamePad.getYaxisData();
  if(left!=right){if(left)m|=BTN_LEFT;else m|=BTN_RIGHT;}
  else if(!left&&!right){if(x < -2)m|=BTN_LEFT;else if(x > 2)m|=BTN_RIGHT;}
  if(up!=down){if(up)m|=BTN_UP;else m|=BTN_DOWN;}
  else if(!up&&!down){if(y > 2)m|=BTN_UP;else if(y < -2)m|=BTN_DOWN;}
  if(GamePad.isCrossPressed()||GamePad.isCirclePressed())m|=BTN_A;
  if(GamePad.isSquarePressed()||GamePad.isTrianglePressed())m|=BTN_B;
  if(GamePad.isSelectPressed())m|=BTN_SELECT;
  if(GamePad.isStartPressed())m|=BTN_START;
  return m;
}
static bool isGbName(const char *name) {
  return GbcS3Paths::isRomPath(name);
}
static bool scanGameNames() {
  gameCount=0;menuSelection=menuTop=visibleSelection=visibleCount=0;
  Serial.printf("[SCAN] Enumerando %s\n",GbcS3Paths::ROMS);
  File root=SD.open(GbcS3Paths::ROMS,FILE_READ);
  if(!root||!root.isDirectory()){
    if(root)root.close();
    sdCatalogVerified=false;
    Serial.println("[SCAN][FATAL] diretorio GBC ausente/inacessivel");
    return false;
  }
  sdCatalogVerified=true;
  uint32_t entries=0,skipped=0;const uint32_t t0=millis();
  for(File f=root.openNextFile();f&&gameCount<Config::MAX_GAMES;f=root.openNextFile()){
    ++entries;
    if(!f.isDirectory()){
      String name=String(f.name());
      const int slash=name.lastIndexOf('/');
      String base=slash>=0?name.substring((size_t)slash+1):name;
      if(isGbName(base.c_str())){
        String full=String(GbcS3Paths::ROMS)+"/"+base;
        if(full.length()<Config::PATH_CAP){
          char stem[Config::NAME_CAP] = {};
          if(GbcS3Paths::makeRomStem(stem,sizeof(stem),base.c_str())){
            GbGame &g=gameList[gameCount++];
            snprintf(g.path,sizeof(g.path),"%s",full.c_str());
            snprintf(g.title,sizeof(g.title),"%s",stem);
          } else ++skipped;
        }else ++skipped;
      }else ++skipped;
    }else ++skipped;
    f.close();if((entries&15)==0)delay(1);
  }
  root.close();
  for(uint16_t i=1;i<gameCount;i++){
    GbGame tmp=gameList[i];int j=i;
    while(j>0&&strcasecmp(gameList[j-1].title,tmp.title)>0){gameList[j]=gameList[j-1];--j;}
    gameList[j]=tmp;
  }
  Serial.printf("[SCAN][DONE] jogos=%u entradas=%lu ignorados=%lu elapsed=%lu ms%s\n",
    (unsigned)gameCount,(unsigned long)entries,(unsigned long)skipped,(unsigned long)(millis()-t0),
    gameCount==Config::MAX_GAMES?" LIMIT=500":"");
  return true;
}
static bool searchMatches(const char *title) {
  if(!librarySearch[0])return true;
  for(const char *p=title;*p;p++){
    unsigned i=0;while(librarySearch[i]&&p[i]&&toupper((unsigned char)p[i])==toupper((unsigned char)librarySearch[i]))i++;
    if(!librarySearch[i])return true;
  }
  return false;
}
static int gameIndexById(uint32_t id) {
  for(unsigned i=0;i<gameCount;i++)if(HaloLibrary::gameId(gameList[i].path)==id)return (int)i;
  return -1;
}
static void buildVisible() {
  uint32_t old=0;
  if(visibleCount&&visibleSelection<visibleCount)old=HaloLibrary::gameId(gameList[visibleGames[visibleSelection]].path);
  visibleCount=0;visibleSelection=0;menuTop=0;
  if(libraryView==2){
    for(unsigned r=0;r<HaloLibrary::recentCount();r++){
      const int i=gameIndexById(HaloLibrary::recent(r));
      if(i>=0&&searchMatches(gameList[i].title))visibleGames[visibleCount++]=(uint16_t)i;
    }
  }else{
    for(uint16_t i=0;i<gameCount;i++){
      if(libraryView==1&&!HaloLibrary::isFavorite(HaloLibrary::gameId(gameList[i].path)))continue;
      if(searchMatches(gameList[i].title))visibleGames[visibleCount++]=i;
    }
  }
  for(unsigned i=0;i<visibleCount;i++)if(HaloLibrary::gameId(gameList[visibleGames[i]].path)==old){visibleSelection=(uint16_t)i;break;}
  Serial.printf("[GBC][LIB][FILTER] tipo=%u busca=%s jogos=%u\n",(unsigned)libraryView,librarySearch,(unsigned)visibleCount);
}
static void drawKeyboard() {
  tft.fillScreen(0x000B);tft.fillRect(0,0,320,29,TFT_NAVY);
  tft.setTextSize(2);tft.setTextColor(TFT_CYAN,TFT_NAVY);tft.setCursor(6,7);tft.print("BUSCAR ROM");
  tft.fillRoundRect(6,36,308,30,3,0x2104);
  tft.setTextColor(TFT_WHITE,0x2104);tft.setTextSize(2);tft.setCursor(15,43);tft.printf("%-12.12s",librarySearch);
  for(unsigned i=0;i<28;i++){
    unsigned x=8+(i%7)*44,y=72+(i/7)*32;const bool hot=i==keyboardCursor;
    const uint16_t bg=hot?0x03FF:0x18C3;
    tft.fillRoundRect(x,y,39,29,4,bg);tft.setTextColor(hot?TFT_BLACK:TFT_WHITE,bg);tft.setTextSize(i==27?1:2);
    tft.setCursor(x+(i==27?7:12),y+(i==27?9:6));
    if(i<26)tft.printf("%c",(char)('A'+i));else tft.print(i==26?"_":"OK");
  }
  tft.setTextSize(1);tft.setTextColor(TFT_LIGHTGREY,0x000B);tft.setCursor(6,213);tft.print("X: inserir | []: apagar | TRI: sair");
}
static void drawResumeConfirm() {
  tft.fillScreen(0x000B);tft.setTextSize(2);tft.setTextColor(TFT_CYAN,0x000B);tft.setCursor(22,38);tft.print("CONTINUAR JOGO?");
  const int last=gameIndexById(HaloLibrary::lastGame());
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,0x000B);tft.setCursor(10,88);
  if(last>=0)tft.printf("%.42s",gameList[last].title);else tft.print("Nenhum jogo recente");
  tft.setCursor(18,147);tft.setTextSize(2);tft.setTextColor(TFT_GREEN,0x000B);tft.print("X = SIM");
  tft.setCursor(18,177);tft.setTextColor(TFT_YELLOW,0x000B);tft.print("B = VOLTAR");
}
static void drawMenuFrame(bool incremental) {
  static uint16_t drawnTop=0,drawnSelection=0;
  GbcVideoOutput::invalidate();
  if(keyboardOpen){GbcS3Covers::cancel();drawKeyboard();return;}
  if(resumePrompt){GbcS3Covers::cancel();drawResumeConfirm();return;}
  if(!incremental){
  tft.fillScreen(0x000B);tft.fillRect(0,0,320,28,TFT_NAVY);
  tft.setTextSize(2);tft.setTextColor(TFT_CYAN,TFT_NAVY);tft.setCursor(8,5);tft.print("GBC-S3");
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,TFT_NAVY);tft.setCursor(252,10);tft.printf("%u ROMS",(unsigned)gameCount);
  if(!sdCatalogVerified){tft.setTextColor(TFT_RED,0x000B);tft.setCursor(9,101);tft.print("SD OFFLINE - VERIFIQUE O CARTAO");return;}
  const int last=gameIndexById(HaloLibrary::lastGame());
  const uint16_t bar=last>=0?0x01D5:0x1082;
  tft.fillRoundRect(4,32,312,27,4,bar);tft.setTextSize(1);tft.setTextColor(TFT_WHITE,bar);tft.setCursor(9,42);
  if(last>=0){tft.print("START: CONTINUAR ");char lastName[23];snprintf(lastName,sizeof(lastName),"%.22s",gameList[last].title);tft.print(lastName);}
  else tft.print("START: CONTINUAR - SEM JOGO RECENTE");
  tft.setCursor(7,67);tft.setTextColor(TFT_YELLOW,0x000B);tft.printf("<%s>",libraryView==0?"TODOS":libraryView==1?"FAVORITOS":"RECENTES");
  tft.drawFastVLine(194,80,138,0x5AEB);
  }
  if(visibleSelection<menuTop)menuTop=visibleSelection;
  if(visibleSelection>=menuTop+Config::MENU_ROWS)menuTop=visibleSelection-Config::MENU_ROWS+1;
  if(!visibleCount){tft.setTextColor(TFT_ORANGE,0x000B);tft.setCursor(7,105);tft.print("NENHUM JOGO .GB/.GBC");}
  for(unsigned row=0;row<Config::MENU_ROWS;row++){
    if(incremental && drawnTop==menuTop && menuTop+row!=drawnSelection && menuTop+row!=visibleSelection)continue;
    const unsigned vi=menuTop+row;if(vi>=visibleCount)break;
    const uint16_t gi=visibleGames[vi];const bool focus=vi==visibleSelection;
    const bool fav=HaloLibrary::isFavorite(HaloLibrary::gameId(gameList[gi].path));
    const uint16_t bg=focus?0x03B9:0x000B;const int y=84+(int)row*18;
    tft.fillRoundRect(3,y-2,187,17,3,bg);tft.setTextSize(1);tft.setTextColor(focus?TFT_WHITE:TFT_LIGHTGREY,bg);tft.setCursor(7,y+2);
    tft.printf("%c%c",focus?'>':' ',fav?'*':' ');
    char shortTitle[27];const char *name=gameList[gi].title;
    if(strlen(name)>26){memcpy(shortTitle,name,23);memcpy(shortTitle+23,"...",4);}else snprintf(shortTitle,sizeof(shortTitle),"%s",name);
    tft.print(shortTitle);
  }
  tft.fillRoundRect(199,76,117,142,5,0x1082);
  if(visibleCount)GbcS3Covers::draw(tft,gameList[visibleGames[visibleSelection]].path,205,79);
  else GbcS3Covers::drawMissing(tft,205,79);
  drawnTop=menuTop;drawnSelection=visibleSelection;
  if(!incremental){tft.drawFastHLine(0,220,320,TFT_DARKGREY);tft.setTextSize(1);tft.setTextColor(TFT_YELLOW,0x000B);tft.setCursor(5,225);
    tft.print("X:JOGAR  []:FAV  TRI:BUSCAR  <>:FILTRO");}
}

static void drawMenu(){drawMenuFrame(false);}
static void drawMenuSelection(){drawMenuFrame(true);}

static uint32_t fnvPath(const char *s){uint32_t h=2166136261u;if(!s)return h;while(*s){h^=(uint8_t)*s++;h*=16777619u;}return h;}
static void freeCartridge(){
  GbcVideoOutput::invalidate();
  GbcCore::unloadRom();
  romSize=cartRamSize=0;activeRomHash=0;cartRamDirty=false;cartBatteryBacked=false;activePath[0]='\0';
}
static void saveName(char *out,size_t cap){if(!GbcS3Paths::makeSramPath(out,cap,fnvPath(activePath),activeRomHash)&&cap)out[0]='\0';}
static bool readSaveCandidate(const char *path){
  GbcVideoOutput::invalidate();
  uint8_t *cartRam=GbcCore::cartRamData();
  if(!cartRam||!cartRamSize)return false;
  File f=SD.open(path,FILE_READ);if(!f)return false;
  if(f.size()!=cartRamSize){f.close();return false;}
  size_t n=f.read(cartRam,cartRamSize);f.close();return n==cartRamSize;
}
static bool loadCartSave(){
  if(!Config::EXPERIMENTAL_SD_SAVES||!cartBatteryBacked||!cartRamSize)return false;
  char path[96],bak[104];saveName(path,sizeof(path));if(!path[0])return false;
  snprintf(bak,sizeof(bak),"%s.bak",path);
  if(readSaveCandidate(path))return true;
  if(readSaveCandidate(bak)){Serial.printf("[GBC][SRAM][RECOVER] backup %s\n",bak);return true;}
  if(uint8_t *cartRam=GbcCore::cartRamData())memset(cartRam,0,cartRamSize);
  return false;
}
static bool writeCartSave(){
  GbcVideoOutput::invalidate();
  uint8_t *cartRam=GbcCore::cartRamData();
  if(!cartBatteryBacked||!cartRam||!cartRamSize||!cartRamDirty)return true;
  if(!Config::EXPERIMENTAL_SD_SAVES){Serial.println("[GBC][SRAM][DISABLED] habilite GBC_ENABLE_SRAM_SAVES apos validar SD+BLE");return false;}
  if(!SD.exists(GbcS3Paths::SAVE)&&!SD.mkdir(GbcS3Paths::SAVE))return false;
  char path[96],tmp[104],bak[104];saveName(path,sizeof(path));if(!path[0])return false;
  snprintf(tmp,sizeof(tmp),"%s.tmp",path);snprintf(bak,sizeof(bak),"%s.bak",path);
  if(SD.exists(tmp)&&!SD.remove(tmp))return false;
  File f=SD.open(tmp,FILE_WRITE);if(!f)return false;
  size_t written=f.write(cartRam,cartRamSize);f.flush();f.close();
  if(written!=cartRamSize){SD.remove(tmp);return false;}
  File verify=SD.open(tmp,FILE_READ);bool ok=verify&&verify.size()==cartRamSize;
  uint8_t check[512];size_t offset=0;
  while(ok&&offset<cartRamSize){size_t want=(cartRamSize-offset)>sizeof(check)?sizeof(check):(cartRamSize-offset);size_t got=verify.read(check,want);if(got!=want||memcmp(check,cartRam+offset,want)!=0)ok=false;else offset+=got;}
  if(verify)verify.close();if(!ok){SD.remove(tmp);return false;}
  bool hadMain=SD.exists(path);
  if(hadMain){if(SD.exists(bak)&&!SD.remove(bak))return false;if(!SD.rename(path,bak))return false;}
  if(!SD.rename(tmp,path)){if(hadMain&&!SD.exists(path)&&SD.exists(bak))SD.rename(bak,path);return false;}
  cartRamDirty=false;Serial.printf("[GBC][SRAM][SAVE] bytes=%u path=%s\n",(unsigned)cartRamSize,path);return true;
}
static void loadRtcSave(){
  GbcVideoOutput::invalidate();
  if(!GbcCore::hasRtc())return;
  char path[96],vfs[300];
  if(!GbcS3Paths::makeRtcPath(path,sizeof(path),fnvPath(activePath),activeRomHash))return;
  if(GbcS3Paths::makeVfsPath(vfs,sizeof(vfs),path))GbcCore::loadRtcFile(vfs);
}
static bool writeRtcSave(){
  GbcVideoOutput::invalidate();
  if(!GbcCore::hasRtc())return true;
  if(!SD.exists(GbcS3Paths::SAVE)&&!SD.mkdir(GbcS3Paths::SAVE))return false;
  char path[96],tmp[104],bak[104],vfs[300];
  if(!GbcS3Paths::makeRtcPath(path,sizeof(path),fnvPath(activePath),activeRomHash))return false;
  snprintf(tmp,sizeof(tmp),"%s.tmp",path);snprintf(bak,sizeof(bak),"%s.bak",path);if(!GbcS3Paths::makeVfsPath(vfs,sizeof(vfs),tmp))return false;
  if(SD.exists(tmp)&&!SD.remove(tmp))return false;
  GbcCore::saveRtcFile(vfs);
  File check=SD.open(tmp,FILE_READ);bool ok=check&&check.size()>0;if(check)check.close();
  if(!ok){if(SD.exists(tmp))SD.remove(tmp);return false;}
  bool hadMain=SD.exists(path);
  if(hadMain){if(SD.exists(bak)&&!SD.remove(bak))return false;if(!SD.rename(path,bak))return false;}
  if(!SD.rename(tmp,path)){if(hadMain&&!SD.exists(path)&&SD.exists(bak))SD.rename(bak,path);return false;}
  Serial.printf("[GBC][RTC][SAVE] %s\n",path);return true;
}
static bool loadRom(const char *path){
  romIoError=0;errno=0;
  GbcVideoOutput::invalidate();
  File f=SD.open(path,FILE_READ);
  if(!f||f.isDirectory()){romIoError=errno?errno:EIO;if(f)f.close();Serial.printf("[GBC][ROM][FAIL] nao abriu %s\n",path);return false;}
  const size_t sz=(size_t)f.size();
  if(sz<0x150||sz>Config::ROM_LIMIT){Serial.printf("[GBC][ROM][REJECT] tamanho=%u\n",(unsigned)sz);f.close();return false;}
  uint8_t header[0x150]={};
  const bool headerRead=f.read(header,sizeof(header))==sizeof(header);
  const int headerErr=errno;f.close();
  if(!headerRead)romIoError=headerErr?headerErr:EIO;
  if(!headerRead){Serial.println("[GBC][ROM][FAIL] cabecalho incompleto");return false;}
  const uint8_t cartType=header[0x147],romCode=header[0x148],ramCode=header[0x149];
  if(romCode>7||sz!=(32768UL<<romCode)||ramCode>5){Serial.printf("[GBC][ROM][REJECT] header ROM=%u RAM=%u bytes=%u\n",romCode,ramCode,(unsigned)sz);return false;}
  if(!GbcCore::supportsCartridgeType(cartType)){Serial.printf("[GBC][ROM][UNSUPPORTED_MAPPER] type=0x%02X; nao iniciar com emulacao incorreta\n",cartType);return false;}
  snprintf(activePath,sizeof(activePath),"%s",path);romSize=sz;
  char vfsPath[300];if(!GbcS3Paths::makeVfsPath(vfsPath,sizeof(vfsPath),path)){activePath[0]='\0';romSize=0;return false;}
  if(!GbcCore::loadRomPath(vfsPath)){romIoError=GbcCore::lastLoadIoError();Serial.println("[GBC][CORE][LOAD_FAIL] GNUBoy recusou a ROM");activePath[0]='\0';romSize=0;activeRomHash=0;return false;}
  activeRomHash=GbcCore::romHash();
  cartRamSize=GbcCore::cartRamSize();cartBatteryBacked=GbcCore::hasBatteryRam();cartRamDirty=false;
  Serial.printf("[GBC][ROM][OK] %s bytes=%u hash=%08lX CGB=%s type=%02X SRAM=%u battery=%s\n",path,(unsigned)sz,(unsigned long)activeRomHash,GbcCore::colorMode()?"SIM":"NAO",GbcCore::cartType(),(unsigned)cartRamSize,cartBatteryBacked?"SIM":"NAO");
  return true;
}
static bool startGame(uint16_t index,bool fromLast=false){
  if(!gameList||index>=gameCount||playing)return false;
  if(!sdOnline&&!recoverSDOnMenu(ENODEV)){statusScreen("SD INDISPONIVEL","Confira cartao e conexoes");delay(900);drawMenu();return false;}
  GbcS3Covers::cancel();
  freeCartridge();const char *path=gameList[index].path;
  Serial.printf("[MENU][START] %s ultimo=%u\n",path,(unsigned)fromLast);
  statusScreen("CARREGANDO GBCC","MicroSD -> GNUBoy");
  bool loaded=loadRom(path);
  const bool ioFailure=romIoError==EIO||romIoError==EBADF||romIoError==ENODEV||romIoError==ETIMEDOUT;
  if(!loaded && ioFailure){
    const int reason=romIoError;
    Serial.printf("[SD][ROM][RETRY] errno=%d; uma remontagem, sem repetir indefinidamente\n",reason);
    sdOnline=false;GbcCore::unloadRom();
    if(recoverSDOnMenu(reason))loaded=loadRom(path);
  }
  if(!loaded){statusScreen(romIoError?"FALHA DE LEITURA SD":"ROM NAO SUPORTADA",romIoError?"Confira cartao; veja Monitor Serial":"Revise cabecalho/tamanho e serial");delay(700);drawMenu();return false;}
  if(cartRamSize){memset(GbcCore::cartRamData(),0,cartRamSize);if(cartBatteryBacked&&loadCartSave())Serial.println("[GBC][SRAM][LOAD] save de cartucho carregado");}
  loadRtcSave();
  GbcSaveState::selectGame(activePath,activeRomHash,(uint32_t)romSize);
  GbcCore::setButtons(0);memset(coreFrame,0,GbcCore::FRAME_BYTES);
  frameCount=0;exitHoldMs=0;nextFrameUs=esp_timer_get_time();
  paused=false;pauseNeedsRelease=false;pauseAwaitActionRelease=false;leaveGameRequested=false;gameWaitForRelease=false;
  menuSelection=index;HaloLibrary::opened(HaloLibrary::gameId(path));playing=true;
  if(fromLast&&GbcSaveState::present(9)){
    if(GbcSaveState::load(9)){cartRamDirty=cartBatteryBacked&&Config::EXPERIMENTAL_SD_SAVES;frameCount=0;nextFrameUs=esp_timer_get_time();Serial.println("[GBC][RESUME][AUTO] estado automatico restaurado");}
    else Serial.println("[GBC][RESUME][AUTO] estado indisponivel/invalido; inicio normal");
  }
  GbcAudioOutput::pause(false);tft.fillScreen(TFT_BLACK);
  Serial.printf("[GAME][GBC] %s ROM=%u SRAM=%u modo=%s\n",activePath,(unsigned)romSize,(unsigned)cartRamSize,GbcCore::colorMode()?"CGB":"DMG");
  reportMemory("game-start");
  nextFrameUs=esp_timer_get_time();return true;
}
static void renderGame(){
  GbcVideoOutput::invalidate();
  if(videoMode==RAPIDO)tft.pushImage(FAST_X0,FAST_Y0,LCD_W,LCD_H,coreFrame);
  else{
    for(unsigned row=0;row<SCREEN_H;row+=VIDEO_STRIPE_ROWS){
      const unsigned rows=(SCREEN_H-row<VIDEO_STRIPE_ROWS)?SCREEN_H-row:VIDEO_STRIPE_ROWS;
      gbc_scale_rgb565_stripe(coreFrame,rgbFrame,row,rows);
      tft.pushImage(X0,Y0+row,SCREEN_W,rows,rgbFrame);
    }
  }
}
static void drawPause(){
  GbcVideoOutput::invalidate();
  const uint16_t bg=0x0000,panel=0x1082,selectedBg=0x0399;
  tft.fillScreen(bg);
  tft.fillRect(0,0,320,32,TFT_NAVY);
  tft.setTextSize(2);tft.setTextColor(TFT_CYAN,TFT_NAVY);
  tft.setCursor(pauseSettings?45:65,8);tft.print(pauseSettings?"VIDEO / AUDIO":"JOGO / SAVE");
  if(!pauseSettings){
    const char *labels[8]={"CONTINUAR","SALVAR ESTADO","CARREGAR ESTADO","SLOT","AUTO SAVE","REINICIAR JOGO","CONFIGURACOES","SAIR DO JOGO"};
    for(uint8_t n=0;n<8;n++){
      const int y=35+21*n;const bool selected=n==pauseSelection;
      const uint16_t rowBg=selected?selectedBg:panel;
      tft.fillRect(5,y,310,20,rowBg);
      tft.setTextColor(selected?TFT_YELLOW:TFT_WHITE,rowBg);tft.setTextSize(1);tft.setCursor(10,y+5);
      if(n==3){
        const int slot=SAVE_SLOT_IDS[selectedSaveSlot];
        tft.printf("%c SLOT: %s [%s]",selected?'>':' ',slot==9?"AUTO":(slot==0?"0":slot==1?"1":slot==2?"2":"3"),GbcSaveState::present(slot)?"SALVO":"VAZIO");
      }else if(n==4){
        tft.printf("%c AUTO SAVE: %s (120s)",selected?'>':' ',GbcSaveState::enabled()?"ON":"OFF");
      }else tft.printf("%c %s",selected?'>':' ',labels[n]);
    }
    tft.setTextSize(1);tft.setTextColor(TFT_LIGHTGREY,bg);tft.setCursor(5,208);tft.print("D-PAD: navegar / slot ESQ-DIR");
    tft.setCursor(5,222);tft.print("X: confirmar   B: continuar");
  }else{
    for(uint8_t n=0;n<5;n++){
      const int y=43+31*n;const bool selected=n==pauseSettingsSelection;
      const uint16_t rowBg=selected?selectedBg:panel;
      tft.fillRect(8,y,304,29,rowBg);tft.setTextSize(1);
      tft.setTextColor(selected?TFT_YELLOW:TFT_WHITE,rowBg);tft.setCursor(15,y+10);
      if(n==0)tft.printf("%c PROPORCAO: %s",selected?'>':' ',videoMode==AMPLIADO?"10:9":"NATIVA");
      else if(n==1)tft.printf("%c %s",selected?'>':' ',!GbcAudioOutput::initialized()?"AUDIO I2S: FALHOU":GbcAudioOutput::enabled()?"AUDIO MAX98357A: ON":"AUDIO MAX98357A: OFF");
      else if(n==2)tft.printf("%c VOLUME I2S: %d%%",selected?'>':' ',GbcAudioOutput::volume());
      else if(n==3)tft.printf("%c INSPECIONAR PIXEL",selected?'>':' ');
      else tft.printf("%c VOLTAR",selected?'>':' ');
    }
    tft.setTextSize(1);tft.setTextColor(TFT_LIGHTGREY,bg);tft.setCursor(12,222);tft.print("X alterar | ESQ/DIR volume/aspecto");
  }
  if(pauseStatusText[0]&&(int32_t)(pauseStatusUntil-millis())>0){
    tft.fillRect(0,204,320,14,bg);tft.setTextSize(1);
    tft.setTextColor(pauseStatusOk?TFT_GREEN:TFT_RED,bg);tft.setCursor(5,207);tft.print(pauseStatusText);
  }
}
static void pauseShowStatus(const char *message,bool ok){
  snprintf(pauseStatusText,sizeof(pauseStatusText),"%.70s",message?message:"");
  pauseStatusOk=ok;pauseStatusUntil=millis()+1800;
  Serial.printf("[GBC][MENU]%s %s\n",ok?"[OK]":"[WARN]",pauseStatusText);
  drawPause();
}
static void pauseInspectPixel(){
  int x=80,y=72;uint16_t prior=readDabbleButtons();bool redraw=true;
  Serial.println("[GBC][PIXEL] D-pad mover; X registra RGB565; B volta");
  for(;;){
    Dabble.processInput();const uint16_t buttons=readDabbleButtons();
    const uint16_t pressed=(uint16_t)(buttons&~prior);prior=buttons;
    if(pressed&BTN_B)break;
    if(pressed&BTN_UP){if(y>0)--y;redraw=true;}
    if(pressed&BTN_DOWN){if(y<LCD_H-1)++y;redraw=true;}
    if(pressed&BTN_LEFT){if(x>0)--x;redraw=true;}
    if(pressed&BTN_RIGHT){if(x<LCD_W-1)++x;redraw=true;}
    if(pressed&BTN_A){Serial.printf("[GBC][PIXEL] x=%d y=%d RGB565=%04X\n",x,y,coreFrame[(size_t)y*LCD_W+x]);redraw=true;}
    if(redraw){
      renderGame();const int px=(videoMode==RAPIDO?FAST_X0+x:X0+(x*3)/2);const int py=(videoMode==RAPIDO?FAST_Y0+y:Y0+(y*3)/2);
      tft.drawFastHLine(px-5<0?0:px-5,py,11,TFT_YELLOW);tft.drawFastVLine(px,py-5<0?0:py-5,11,TFT_YELLOW);
      tft.fillRect(0,218,320,22,TFT_BLACK);tft.setTextSize(1);tft.setTextColor(TFT_YELLOW,TFT_BLACK);
      tft.setCursor(5,223);tft.printf("X:%03d Y:%03d RGB565:%04X X=LOG B=SAIR",x,y,coreFrame[(size_t)y*LCD_W+x]);redraw=false;
    }
    delay(20);
  }
  drawPause();
}
static void resumeGame(){GbcVideoOutput::invalidate();paused=false;pauseSettings=false;pauseNeedsRelease=false;pauseAwaitActionRelease=false;gameWaitForRelease=true;exitHoldMs=0;GbcAudioOutput::pause(false);tft.fillScreen(TFT_BLACK);frameCount=0;nextFrameUs=esp_timer_get_time();}
static void leaveGame(){
  GbcVideoOutput::invalidate();
  GbcAudioOutput::pause(true);
  if(!writeCartSave())Serial.println("[GBC][SRAM][WARN] SRAM nao persistida");
  if(!writeRtcSave())Serial.println("[GBC][RTC][WARN] RTC nao persistido");
  playing=false;paused=false;pauseSettings=false;leaveGameRequested=false;GbcCore::setButtons(0);freeCartridge();
  buildVisible();drawMenu();lastMenuInputMs=millis();reportMemory("menu-return");Serial.println("[GAME] retorno ao menu sem reboot");
}
static void restartCurrentGame(){
  GbcVideoOutput::invalidate();
  const uint16_t index=menuSelection;
  if(Config::EXPERIMENTAL_SD_SAVES&&cartBatteryBacked&&cartRamDirty&&!writeCartSave())Serial.println("[GBC][SRAM][WARN] SRAM nao foi salva antes do reinicio");
  if(!writeRtcSave())Serial.println("[GBC][RTC][WARN] RTC nao foi salvo antes do reinicio");
  GbcAudioOutput::pause(true);playing=false;paused=false;pauseSettings=false;leaveGameRequested=false;GbcCore::setButtons(0);freeCartridge();
  Serial.println("[GBC][RESET] reiniciando jogo atual sem reboot da placa");
  if(!startGame(index,false)){buildVisible();drawMenu();}
}
static void processPause(uint16_t buttons){
  if(pauseNeedsRelease){if((buttons&(BTN_START|BTN_SELECT))==0)pauseNeedsRelease=false;delay(5);return;}
  if(pauseAwaitActionRelease){if((buttons&(BTN_A|BTN_B))==0)pauseAwaitActionRelease=false;delay(5);return;}
  const uint32_t now=millis();
  if(pauseStatusText[0]&&(int32_t)(pauseStatusUntil-now)<=0){pauseStatusText[0]='\0';drawPause();}
  if(now-lastMenuInputMs<180){delay(5);return;}
  if(buttons&BTN_UP){
    if(pauseSettings)pauseSettingsSelection=(uint8_t)((pauseSettingsSelection+4)%5);
    else pauseSelection=(uint8_t)((pauseSelection+7)%8);
    lastMenuInputMs=now;drawPause();
  }else if(buttons&BTN_DOWN){
    if(pauseSettings)pauseSettingsSelection=(uint8_t)((pauseSettingsSelection+1)%5);
    else pauseSelection=(uint8_t)((pauseSelection+1)%8);
    lastMenuInputMs=now;drawPause();
  }else if(!pauseSettings&&pauseSelection==3&&(buttons&(BTN_LEFT|BTN_RIGHT))){
    const int delta=(buttons&BTN_RIGHT)?1:-1;
    selectedSaveSlot=(uint8_t)((selectedSaveSlot+5+delta)%5);lastMenuInputMs=now;drawPause();
  }else if(pauseSettings&&(buttons&(BTN_LEFT|BTN_RIGHT))){
    const int delta=(buttons&BTN_RIGHT)?5:-5;lastMenuInputMs=now;
    if(pauseSettingsSelection==0){videoMode=videoMode==AMPLIADO?RAPIDO:AMPLIADO;Serial.printf("[VIDEO] proporcao=%s\n",videoMode==AMPLIADO?"4:3":"NATIVA");}
    else if(pauseSettingsSelection==1){GbcAudioOutput::setEnabled(!GbcAudioOutput::enabled());GbcAudioOutput::pause(true);Serial.printf("[GBC][AUDIO] MAX98357A=%s\n",GbcAudioOutput::enabled()?"ON":"OFF");}
    else if(pauseSettingsSelection==2){GbcAudioOutput::setVolume(GbcAudioOutput::volume()+delta);HaloLibrary::setVolume(GbcAudioOutput::volume());}
    drawPause();
  }else if(buttons&BTN_B){
    pauseAwaitActionRelease=true;lastMenuInputMs=now;
    if(pauseSettings){pauseSettings=false;pauseSelection=6;drawPause();}
    else resumeGame();
  }else if(buttons&BTN_A){
    pauseAwaitActionRelease=true;lastMenuInputMs=now;
    if(pauseSettings){
      switch(pauseSettingsSelection){
        case 0: videoMode=videoMode==AMPLIADO?RAPIDO:AMPLIADO;drawPause();break;
        case 1: GbcAudioOutput::setEnabled(!GbcAudioOutput::enabled());GbcAudioOutput::pause(true);drawPause();break;
        case 2: GbcAudioOutput::setVolume(GbcAudioOutput::volume()+10);if(GbcAudioOutput::volume()>100)GbcAudioOutput::setVolume(0);HaloLibrary::setVolume(GbcAudioOutput::volume());drawPause();break;
        case 3: pauseInspectPixel();break;
        default: pauseSettings=false;pauseSelection=6;drawPause();break;
      }
    }else{
      switch(pauseSelection){
        case 0: resumeGame();break;
        case 1:{
          const bool ok=GbcSaveState::save(SAVE_SLOT_IDS[selectedSaveSlot]);
          pauseShowStatus(ok?"ESTADO SALVO":"FALHA AO SALVAR ESTADO",ok);break;
        }
        case 2:{
          const bool ok=GbcSaveState::load(SAVE_SLOT_IDS[selectedSaveSlot]);
          if(ok){
            cartRamDirty=cartBatteryBacked&&Config::EXPERIMENTAL_SD_SAVES;
            frameCount=0;nextFrameUs=esp_timer_get_time();
            pauseShowStatus("ESTADO CARREGADO",true);delay(900);resumeGame();
          }else pauseShowStatus("ESTADO AUSENTE / INVALIDO",false);
          break;
        }
        case 3: selectedSaveSlot=(uint8_t)((selectedSaveSlot+1)%5);drawPause();break;
        case 4: GbcSaveState::setEnabled(!GbcSaveState::enabled());drawPause();break;
        case 5: restartCurrentGame();break;
        case 6: pauseSettings=true;pauseSettingsSelection=0;drawPause();break;
        default:
          if(GbcSaveState::enabled()){
            const bool ok=GbcSaveState::save(9);
            Serial.printf("[GBC][STATE][AUTO][EXIT] %s\n",ok?"OK":"FALHA");
          }
          leaveGameRequested=true;break;
      }
    }
  }
  delay(5);
}

void setup(){
  Serial.begin(115200);delay(700);
  Serial.println("[BUILD] GBC-S3 DEV 0.3.8 SEM-TELEMETRIA / watchdog+stack fix / SD=1MHz / loopStack=24576");
  Serial.println("\n===============================================\n GBC-S3 / ESP32-S3 -- GNUBOY CGB\n===============================================");
  pinMode(TFT_CS,OUTPUT);pinMode(Config::SD_CS,OUTPUT);digitalWrite(TFT_CS,HIGH);digitalWrite(Config::SD_CS,HIGH);delay(120);
  SPIClass &sharedSPI=tft.getSPIinstance();
  if(!sharedSPI.begin(TFT_SCLK,TFT_MISO,TFT_MOSI,-1)){Serial.println("[SPI][FATAL] begin falhou");while(true)delay(1000);}
  if(!mountSD(SD_BOOT_RETRIES,"BOOT")){
    Serial.println("[SD][FATAL] falha apos tentativas SD-FIRST");tft.init();tft.setRotation(TFT_ROTATION);applyColorPipeline("SD_FAIL");statusScreen("ERRO SD","CS14/MISO/3V3 - desligue/religue");while(true)delay(1000);
  }
  const bool dirsOk=ensureGbDirectories();
  const bool directoryBeforeTFT=probeGbDirectory("PRE-TFT");
  Serial.printf("[SD] mounted card=%lluMB clock=%lu kHz\n",(unsigned long long)(SD.cardSize()/1048576ULL),(unsigned long)(Config::SD_HZ/1000));
  tft.init();tft.setRotation(TFT_ROTATION);applyColorPipeline("BOOT");
  if(tft.width()!=320||tft.height()!=240){Serial.println("[TFT][FATAL] esperava 320x240");while(true)delay(1000);}
  drawBoot();
  gameList=(GbGame*)heap_caps_malloc(sizeof(GbGame)*Config::MAX_GAMES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  coreFrame=(uint16_t*)heap_caps_malloc(GbcCore::FRAME_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  coreFrameInternal=coreFrame!=nullptr;
  if(!coreFrame)coreFrame=(uint16_t*)psAlloc(GbcCore::FRAME_BYTES);
  rgbFrame=(uint16_t*)heap_caps_malloc((size_t)SCREEN_W*VIDEO_STRIPE_ROWS*sizeof(uint16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!gameList||!coreFrame||!rgbFrame){statusScreen("ERRO PSRAM","Falha nos buffers do emulador");while(true)delay(1000);}
  if(!GbcCore::beginPlatform(coreFrame,GbcCore::FRAME_BYTES)){statusScreen("ERRO CORE","PSRAM / cache de tiles GNUBoy");while(true)delay(1000);}
  memset(gameList,0,sizeof(GbGame)*Config::MAX_GAMES);
  const bool directoryAfterTFT=probeGbDirectory("POST-TFT");
  Serial.printf("[SD][TRANSITION] PRE-TFT=%s POST-TFT=%s dirs=%s\n",directoryBeforeTFT?"OK":"FAIL",directoryAfterTFT?"OK":"FAIL",dirsOk?"OK":"FAIL");
  bool catalogOk=dirsOk&&directoryAfterTFT&&scanGameNames();
  tft.fillRect(0,180,320,5,TFT_BLUE);
  const bool spiCheck=probeGbDirectory("AFTER-TFT-DRAW");
  if(!catalogOk||!spiCheck){gameCount=0;sdCatalogVerified=false;Serial.println("[SD][BOOT-RECOVER] montando novamente no menu");
    if(recoverSDOnMenu(EIO)&&ensureGbDirectories()){catalogOk=scanGameNames();}
    if(!catalogOk||!probeGbDirectory("FINAL")){gameCount=0;sdCatalogVerified=false;Serial.println("[SD][BOOT-RECOVER][FAIL] catalogo indisponivel");}
  }
  HaloLibrary::load();buildVisible();GbcS3Covers::begin();
  GbcAudioOutput::setVolume(HaloLibrary::volume());
  if(!GbcAudioOutput::begin())Serial.println("[GBC][AUDIO][WARN] I2S indisponivel; jogo continua sem som");
  Serial.println("[BLE] Dabble.begin(ESP32-S3-GBC-LAB)");Dabble.begin("ESP32-S3-GBC-LAB");
  Serial.printf("[MEM] framebuffer=RGB565 PSRAM freePSRAM=%u\n",(unsigned)ESP.getFreePsram());
  Serial.printf("[SAVE] SRAM de cartucho=%s; save states=%s dir=%s\n",Config::EXPERIMENTAL_SD_SAVES?"EXPERIMENTAL/ON":"OFF POR SEGURANCA",GbcSaveState::enabled()?"ON":"OFF",GbcS3Paths::SAVE);
  Serial.println("[READY] biblioteca GBC independente; ROMs .gb/.gbc e estados GNUBoy proprios");
  Serial.printf("[CORE] GNUBoy GB/CGB + PCM; I2S=%s volume=%d; estado GNUBoy versionado\n",GbcAudioOutput::initialized()?"OK":"FAIL",GbcAudioOutput::volume());
  Serial.println("[AUDIO] GNUBoy PCM 8-bit -> I2S signed-16 MAX98357A GPIO15/16/17; ajuste na pausa");
  Serial.println("[PAUSE] START+SELECT 700ms -> JOGO/SAVE e submenu VIDEO/AUDIO comuns ao NES-S3");
  if(!GbcVideoOutput::begin(tft,rgbFrame)){
    statusScreen("ERRO VIDEO ASYNC","Snapshot/tarefa indisponivel; veja serial");
    Serial.println("[VIDEO][FATAL] Falha ao iniciar tarefa de video; nao executar em modo lento");
    while(true)delay(1000);
  }
  drawMenu();reportMemory("ready");
}

void loop(){
  static uint16_t prev=0;
  static bool prevCross=false,prevSquare=false,prevTriangle=false,prevStart=false,prevCircle=false;
  Dabble.processInput();
  uint16_t buttons=readDabbleButtons();
  if(playing){
    if(leaveGameRequested){leaveGame();return;}
    if(paused){processPause(buttons);return;}
    if(gameWaitForRelease){
      GbcCore::setButtons(0);
      if(buttons==0)gameWaitForRelease=false;
      else{delay(5);return;}
    }
    const bool exitCombo=(buttons&(BTN_START|BTN_SELECT))==(BTN_START|BTN_SELECT);
    if(exitCombo){if(!exitHoldMs)exitHoldMs=millis();else if((uint32_t)(millis()-exitHoldMs)>=700){paused=true;GbcAudioOutput::pause(true);GbcCore::setButtons(0);pauseNeedsRelease=true;pauseSelection=0;pauseSettings=false;pauseSettingsSelection=0;pauseStatusText[0]='\0';drawPause();lastMenuInputMs=millis();return;}buttons=(uint16_t)(buttons&~(BTN_START|BTN_SELECT));}
    else exitHoldMs=0;
    GbcCore::setButtons(buttons);
    const int64_t start=esp_timer_get_time();const int64_t behind=start-nextFrameUs;
    // Save PPU pixel work for frames we may display. All CPU/LCDC/audio ticks
    // still run; a late core gets catch-up frames with no pixel generation.
    const bool renderPixels=gbc_should_render(frameCount,videoMode==RAPIDO,behind,Config::FRAME_US);
    GbcCore::setFrameRendering(renderPixels);
    GbcCore::runFrame();if(cartBatteryBacked&&Config::EXPERIMENTAL_SD_SAVES)cartRamDirty=true;
    if(renderPixels){
      GbcVideoOutput::submit(coreFrame,videoMode==AMPLIADO);
    }
    frameCount++;nextFrameUs+=Config::FRAME_US;
    if(GbcSaveState::autoDue()){
      GbcVideoOutput::invalidate(); // SPI belongs to loopTask until it submits again.
      GbcAudioOutput::pause(true);
      const int saved=GbcSaveState::saveAuto();
      Serial.printf("[GBC][STATE][AUTO][PERIODIC] %s\n",saved==1?"OK":saved<0?"FALHA":"SKIP");
      GbcAudioOutput::pause(false);
      nextFrameUs=esp_timer_get_time();
    }
    // This tick is inside the frame budget, not appended after the deadline.
    vTaskDelay(1);
    const int64_t pacedNow=esp_timer_get_time();
    if(nextFrameUs>pacedNow){const int64_t waitUs=nextFrameUs-pacedNow;if(waitUs>2000)delay((uint32_t)(waitUs/1000-1));while(esp_timer_get_time()<nextFrameUs)delayMicroseconds(50);}
    else if(pacedNow-nextFrameUs>Config::FRAME_US*3)nextFrameUs=pacedNow;
    return;
  }
  const bool riseUp=(buttons&BTN_UP)&&!(prev&BTN_UP),riseDown=(buttons&BTN_DOWN)&&!(prev&BTN_DOWN);
  const bool riseLeft=(buttons&BTN_LEFT)&&!(prev&BTN_LEFT),riseRight=(buttons&BTN_RIGHT)&&!(prev&BTN_RIGHT);
  const bool cross=GamePad.isCrossPressed(),square=GamePad.isSquarePressed(),triangle=GamePad.isTrianglePressed();
  const bool circle=GamePad.isCirclePressed(),start=GamePad.isStartPressed();
  const bool pressX=cross&&!prevCross,pressSquare=square&&!prevSquare,pressTriangle=triangle&&!prevTriangle;
  const bool pressCircle=circle&&!prevCircle,pressStart=start&&!prevStart;
  if(resumePrompt){
    if(pressX){const int idx=gameIndexById(HaloLibrary::lastGame());resumePrompt=false;if(idx>=0){menuSelection=(uint16_t)idx;startGame(menuSelection,true);}else drawMenu();}
    else if(pressSquare||pressCircle||pressTriangle){resumePrompt=false;drawMenu();}
  }else if(keyboardOpen){
    if(riseLeft){keyboardCursor=(uint8_t)((keyboardCursor/7)*7+(keyboardCursor%7+6)%7);drawMenu();}
    if(riseRight){keyboardCursor=(uint8_t)((keyboardCursor/7)*7+(keyboardCursor%7+1)%7);drawMenu();}
    if(riseUp){keyboardCursor=(uint8_t)((keyboardCursor+21)%28);drawMenu();}
    if(riseDown){keyboardCursor=(uint8_t)((keyboardCursor+7)%28);drawMenu();}
    if(pressSquare){size_t n=strlen(librarySearch);if(n)librarySearch[n-1]='\0';buildVisible();drawMenu();}
    if(pressCircle){librarySearch[0]='\0';buildVisible();drawMenu();}
    if(pressTriangle){keyboardOpen=false;drawMenu();}
    if(pressX){if(keyboardCursor==27){keyboardOpen=false;buildVisible();drawMenu();}
      else{size_t n=strlen(librarySearch);if(n<sizeof(librarySearch)-1){librarySearch[n]=keyboardCursor==26?' ':(char)('A'+keyboardCursor);librarySearch[n+1]='\0';buildVisible();}drawMenu();}}
  }else{
    if(riseLeft){libraryView=(uint8_t)((libraryView+2)%3);buildVisible();drawMenu();}
    if(riseRight){libraryView=(uint8_t)((libraryView+1)%3);buildVisible();drawMenu();}
    if(riseUp&&visibleCount){visibleSelection=(uint16_t)((visibleSelection+visibleCount-1)%visibleCount);drawMenuSelection();}
    if(riseDown&&visibleCount){visibleSelection=(uint16_t)((visibleSelection+1)%visibleCount);drawMenuSelection();}
    if(pressSquare&&visibleCount){HaloLibrary::toggleFavorite(HaloLibrary::gameId(gameList[visibleGames[visibleSelection]].path));buildVisible();drawMenu();}
    if(pressTriangle){keyboardOpen=true;drawMenu();}
    if(pressStart&&gameIndexById(HaloLibrary::lastGame())>=0){resumePrompt=true;drawMenu();}
    if(pressX&&visibleCount){menuSelection=visibleGames[visibleSelection];startGame(menuSelection,false);}
  }
  prevCross=cross;prevSquare=square;prevTriangle=triangle;prevCircle=circle;prevStart=start;prev=buttons;
  if(!playing&&!keyboardOpen&&!resumePrompt&&visibleCount)GbcS3Covers::service(tft,gameList[visibleGames[visibleSelection]].path,205,79);
  delay(2);
}
