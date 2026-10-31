#pragma once
#include <stdint.h>
class TFT_eSPI;
namespace GbcVideoOutput {
// stripe must have 240*8 RGB565 pixels and remain allocated for the session.
bool begin(TFT_eSPI &display,uint16_t *stripe);
bool submit(const uint16_t *frame,bool enlarged);
// Only the producer calls drain. No new frame is submitted until it returns.
// Mandatory before any producer-side SD/TFT access, including menus and saves.
void drain();
// Producer only: drain before invalidating after a screen clear/menu/rotation.
// The next snapshot must redraw completely even when its pixels are unchanged.
void invalidate();
}
