#ifndef EMU_H
#define EMU_H

void emu_init(void);
void emu_run();
int emu_run_frame(void);
void emu_reset();
void emu_pause(int paused);
int emu_paused(void);

#endif


