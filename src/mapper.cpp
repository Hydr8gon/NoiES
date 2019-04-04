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
#include <vector>

#include "core.h"
#include "cpu.h"
#include "ppu.h"

namespace mapper
{

uint8_t *rom;
uint32_t vromAddress;

uint8_t type;
uint8_t bankSelect, latch, shift;
uint8_t mmc2VromBanks[4];
uint8_t irqCount, irqLatch;
bool irqEnable, irqReload;

const vector<core::StateItem> stateItems =
{
    { &bankSelect,   sizeof(bankSelect)    },
    { &latch,        sizeof(latch)         },
    { &shift,        sizeof(shift)         },
    { mmc2VromBanks, sizeof(mmc2VromBanks) },
    { &irqCount,     sizeof(irqCount)      },
    { &irqLatch,     sizeof(irqLatch)      },
    { &irqEnable,    sizeof(irqEnable)     },
    { &irqReload,    sizeof(irqReload)     }
};

bool load(FILE *romFile, uint8_t numBanks, uint8_t mapperType)
{
    if (mapperType > 4 && mapperType != 9) // Unknown mapper type
        return false;

    vromAddress = numBanks * 0x4000;
    type = mapperType;

    // Clear the state items
    for (unsigned int i = 0; i < stateItems.size(); i++)
        memset(stateItems[i].pointer, 0, stateItems[i].size);

    // Move the ROM into its own memory, with an extra 8 KB for VRAM if no VROM is present
    uint16_t start = ftell(romFile);
    fseek(romFile, 0, SEEK_END);
    uint32_t size = ftell(romFile) - start;
    delete[] rom;
    rom = new uint8_t[size + ((vromAddress == size) ? 0x2000 : 0)];
    fseek(romFile, start, SEEK_SET);
    fread(rom, 1, size, romFile);
    fclose(romFile);

    // Load the initial banks into system memory
    uint16_t lastSize = (type == 9) ? 0x6000 : 0x4000;
    memcpy(&cpu::memory[0x8000], rom, 0x8000 - lastSize);
    memcpy(&cpu::memory[0xA000], &rom[vromAddress - lastSize], lastSize);
    memcpy(ppu::memory, &rom[vromAddress], 0x2000);

    return true;
}

void mmc1(uint16_t address, uint8_t value)
{
    if (value & 0x80)
    {
        // Reset the shift register
        bankSelect |= 0x0C;
        latch = 0;
        shift = 0;
    }
    else
    {
        // Write a bit to the latch
        latch |= (value & 0x01) << shift++;
    }

    // Pass the 5-bit value to a register
    if (shift == 5)
    {
        if (address >= 0x8000 && address < 0xA000) // Control
        {
            bankSelect = latch;
            ppu::mirrorMode = latch & 0x03;
        }
        else if (address >= 0xA000 && address < 0xC000) // Swap VROM bank 0
        {
            if (bankSelect & 0x10) // 4 KB
                memcpy(ppu::memory, &rom[vromAddress + 0x1000 * latch], 0x1000);
            else // 8 KB
                memcpy(ppu::memory, &rom[vromAddress + 0x1000 * (latch & ~0x01)], 0x2000);
        }
        else if (address >= 0xC000 && address < 0xE000) // Swap VROM bank 1
        {
            if (bankSelect & 0x10) // 4 KB
                memcpy(&ppu::memory[0x1000], &rom[vromAddress + 0x1000 * latch], 0x1000);
        }
        else // Swap ROM banks
        {
            if (!(bankSelect & 0x08)) // 32 KB
            {
                memcpy(&cpu::memory[0x8000], &rom[0x4000 * (latch & ~0x01)], 0x8000);
            }
            else if (bankSelect & 0x04) // 16 KB, bank 1 fixed
            {
                memcpy(&cpu::memory[0x8000], &rom[0x4000 * latch], 0x4000);
                memcpy(&cpu::memory[0xC000], &rom[vromAddress - 0x4000], 0x4000);
            }
            else // 16 KB, bank 0 fixed
            {
                memcpy(&cpu::memory[0xC000], &rom[0x4000 * latch], 0x4000);
                memcpy(&cpu::memory[0x8000], rom, 0x4000);
            }
        }

        latch = 0;
        shift = 0;
    }
}

void unrom(uint16_t address, uint8_t value)
{
    // Swap the first 16 KB ROM bank
    memcpy(&cpu::memory[0x8000], &rom[0x4000 * value], 0x4000);
}

void cnrom(uint16_t address, uint8_t value)
{
    // Swap the 8 KB VROM bank
    memcpy(ppu::memory, &rom[vromAddress + 0x2000 * (value & 0x03)], 0x2000);
}

void mmc3(uint16_t address, uint8_t value)
{
    if (address >= 0x8000 && address < 0xA000)
    {
        if (address % 2 == 0) // Select banks
        {
            bankSelect = value;
            memcpy(&cpu::memory[(value & 0x40) ? 0x8000 : 0xC000], &rom[vromAddress - 0x4000], 0x2000);
        }
        else // Swap banks
        {
            uint8_t bank = bankSelect & 0x07;
            if (bank < 2) // 2 KB VROM banks
            {
                memcpy(&ppu::memory[((bankSelect & 0x80) ? 0x1000 : 0) + 0x800 * bank],
                       &rom[vromAddress + 0x400 * (value & ~0x01)], 0x800);
            }
            else if (bank >= 2 && bank < 6) // 1 KB VROM banks
            {
                memcpy(&ppu::memory[((bankSelect & 0x80) ? 0 : 0x1000) + 0x400 * (bank - 2)],
                       &rom[vromAddress + 0x400 * value], 0x400);
            }
            else if (bank == 6) // Swappable/fixed 8 KB ROM bank
            {
                memcpy(&cpu::memory[(bankSelect & 0x40) ? 0xC000 : 0x8000], &rom[0x2000 * value], 0x2000);
            }
            else // Swappable 8 KB ROM bank
            {
                memcpy(&cpu::memory[0xA000], &rom[0x2000 * value], 0x2000);
            }
        }
    }
    else if (address >= 0xA000 && address < 0xC000)
    {
        if (address % 2 == 0) // Mirroring
        {
            if (ppu::mirrorMode != 4)
                ppu::mirrorMode = 2 + value;
        }
    }
    else if (address >= 0xC000 && address < 0xE000)
    {
        if (address % 2 == 0) // IRQ latch
            irqLatch = value;
        else // IRQ reload
            irqReload = true;
    }
    else // IRQ toggle
    {
        irqEnable = (address % 2 == 1);
    }
}

void mmc2(uint16_t address, uint8_t value)
{
    if (address >= 0xA000 && address < 0xB000) // Swap first 8 KB ROM bank
    {
        memcpy(&cpu::memory[0x8000], &rom[0x2000 * value], 0x2000);
    }
    else if (address < 0xF000) // Select VROM banks
    {
        mmc2VromBanks[(address - 0xB000) / 0x1000] = value;
    }
    else // Mirroring
    {
        if (ppu::mirrorMode != 4)
            ppu::mirrorMode = 2 + value;
    }
}

void registerWrite(uint16_t address, uint8_t value)
{
    switch (type)
    {
        case 1:  mmc1(address, value); break;
        case 2: unrom(address, value); break;
        case 3: cnrom(address, value); break;
        case 4:  mmc3(address, value); break;
        case 9:  mmc2(address, value); break;
    }
}

void mmc3Counter()
{
    // Clock the MMC3 IRQ counter
    if (irqCount == 0 || irqReload)
    {
        // Trigger an IRQ if they're enabled
        if (irqEnable && !irqReload)
            cpu::interrupts[2] = true;

        irqCount = irqLatch;
        irqReload = false;
    }
    else
    {
        irqCount--;
    }
}

void mmc2SetLatch(uint8_t latch, bool value)
{
    if (latch == 0)
        memcpy(ppu::memory, &rom[vromAddress + 0x1000 * mmc2VromBanks[value]], 0x1000);
    else
        memcpy(&ppu::memory[0x1000], &rom[vromAddress + 0x1000 * mmc2VromBanks[2 + value]], 0x1000);
}

void saveState(FILE *state)
{
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fwrite(stateItems[i].pointer, 1, stateItems[i].size, state);
}

void loadState(FILE *state)
{
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fread(stateItems[i].pointer, 1, stateItems[i].size, state);
}

}
