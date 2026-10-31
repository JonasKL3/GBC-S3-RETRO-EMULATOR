#ifndef SAVE_H
#define SAVE_H

#include <stdio.h>

void savestate(FILE *f);
void loadstate(FILE *f);
int loadstate_checked(FILE *f);
int savestate_checked(FILE *f);
int gnuboy_state_header_valid(const unsigned char *buf);

#endif

