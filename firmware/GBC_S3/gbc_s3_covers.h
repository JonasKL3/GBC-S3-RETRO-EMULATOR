#pragma once
class TFT_eSPI;
namespace GbcS3Covers {
  bool begin();
  // Cancel the pending file before ROM loading, keyboard, menus or game start.
  void cancel();
  // Close pending reads and invalidate all cache entries before SD remount.
  void invalidate();
  // Never reads SD: draws a cached cover or a placeholder and schedules a load.
  void draw(TFT_eSPI &tft,const char *romPath,int x,int y);
  // Menu-only incremental loading after input processing, max1024 data bytes.
  void service(TFT_eSPI &tft,const char *romPath,int x,int y);
  void drawMissing(TFT_eSPI &tft,int x,int y);
}
