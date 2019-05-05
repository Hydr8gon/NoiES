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

#include <cstdio>
#include <cstring>

#include "core.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "mapper.h"

namespace core
{

uint8_t globalCycles;

string romName;
bool hasBattery;

int loadRom(string filename)
{
    // Open the file
    FILE *file = fopen(filename.c_str(), "rb");
    if (!file)
    {
        printf("Failed to open ROM!\n");
        return 1;
    }

    // Read the file header
    uint8_t header[0x10];
    fread(header, 1, 0x10, file);

    // Verify that the file is an NES ROM
    char filetype[4];
    memcpy(filetype, header, 3);
    filetype[3] = '\0';
    if (strcmp(filetype, "NES") != 0 || header[3] != 0x1A)
    {
        printf("Invalid ROM format!\n");
        return 2;
    }

    romName = filename.substr(0, filename.rfind("."));

    // Reset the system
    cpu::reset();
    ppu::reset();
    apu::reset();
    globalCycles = 0;

    // Load the trainer into memory if the ROM has one
    if (header[6] & 0x04)
        fread(&cpu::memory[0x7000], 1, 0x200, file);

    // Initialize the ROM mapper
    ppu::mirrorMode = (header[6] & 0x08) ? 4 : 3 - (header[6] & 0x01);
    uint8_t mapperType = header[7] | (header[6] >> 4);
    if (!mapper::load(file, header[4], mapperType))
    {
        printf("Unknown mapper type: %d\n", mapperType);
        return mapperType;
    }

    // Attempt to load a savefile if the ROM has battery-backed SRAM
    if (header[6] & 0x02)
    {
        hasBattery = true;
        FILE *save = fopen((romName + ".sav").c_str(), "rb");
        if (save)
        {
            fread(&cpu::memory[0x6000], 1, 0x2000, save);
            fclose(save);
        }
    }

    return 0;
}

void closeRom()
{
    // Write a savefile if the ROM has battery-backed SRAM
    if (hasBattery)
    {
        FILE *save = fopen((romName + ".sav").c_str(), "wb");
        fwrite(&cpu::memory[0x6000], 1, 0x2000, save);
        fclose(save);
    }
}

void runCycle()
{
    // Run a global cycle
    cpu::runCycle();
    ppu::runCycle();
    apu::runCycle();
    ++globalCycles %= 6;
}

void pressKey(uint8_t pad, uint8_t key)
{
    // Set the bit corresponding to the pressed key
    cpu::inputMasks[pad] |= 1 << key;
}

void releaseKey(uint8_t pad, uint8_t key)
{
    // Clear the bit corresponding to the released key
    cpu::inputMasks[pad] &= ~(1 << key);
}

void saveState()
{
    // Write everything to a state file
    FILE *state = fopen((romName + ".noi").c_str(), "wb");
    cpu::saveState(state);
    ppu::saveState(state);
    apu::saveState(state);
    mapper::saveState(state);
    fwrite(&globalCycles, 1, sizeof(globalCycles), state);
    fclose(state);
}

void loadState()
{
    // Read everything from a state file if it exists
    FILE *state = fopen((romName + ".noi").c_str(), "rb");
    if (state)
    {
        cpu::loadState(state);
        ppu::loadState(state);
        apu::loadState(state);
        mapper::loadState(state);
        fread(&globalCycles, 1, sizeof(globalCycles), state);
        fclose(state);
    }
}

}
