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

#include <cstdint>
#include <string>

using namespace std;

namespace core
{

typedef struct
{
    void *pointer;
    uint32_t size;
} StateItem;

extern uint8_t globalCycles;

int  loadRom(string filename);
void closeRom();

void runCycle();

void pressKey(uint8_t key);
void releaseKey(uint8_t key);

void saveState();
void loadState();

}

#endif // CORE_H
