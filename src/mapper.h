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

#ifndef MAPPER_H
#define MAPPER_H

#include <cstdint>

namespace mapper
{

bool load(FILE *romFile, uint8_t numBanks, uint8_t mapperType);
void registerWrite(uint16_t address, uint8_t value);

void mmc3Counter();
void mmc2SetLatch(uint8_t latch, bool value);

void saveState(FILE *state);
void loadState(FILE *state);

}

#endif // MAPPER_H
