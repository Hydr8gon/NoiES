#ifndef CORE_H
#define CORE_H

#include <stdint.h>

extern uint32_t display[256 * 240];

bool loadRom(char *filename);
void closeRom();

void runCycle();
int16_t audioSample();

void pressKey(uint8_t key);
void releaseKey(uint8_t key);

#endif // CORE_H
