/*
    Copyright 2019 Hydr8gon

    This file is part of NoiES.

    NoiES is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NoiES is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NoiES. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef CORE_H
#define CORE_H

#include <stdint.h>

extern uint32_t displayBuffer[256 * 240];

bool loadRom(char *filename);
void closeRom();

void runCycle();
int16_t audioSample(float pitch);

void pressKey(uint8_t key);
void releaseKey(uint8_t key);

#endif // CORE_H
