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

#include "core.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "mutex.h"

uint8_t cpuMemory[0x10000];
uint8_t ppuMemory[0x4000];
uint8_t sprMemory[0x100];

uint8_t globalCycles;

uint16_t cpuCycles, cpuTargetCycles;
uint16_t programCounter;
uint8_t accumulator, registerX, registerY;
uint8_t flags = 0x24; // NV_BDIZC
uint8_t stackPointer = 0xFF;
bool interrupts[3]; // NMI, RST, BRK

uint16_t scanline, scanlineCycles;
uint16_t ppuAddress, ppuTempAddr;
uint8_t ppuBuffer;
uint8_t scrollX;
uint8_t spriteCount;
bool ppuToggle;

uint16_t frameCounter;
uint16_t pulses[2];
uint8_t dutyCycles[2];
uint8_t volumes[2];
uint8_t lengthCounters[2];
uint8_t envelopeDividers[2];
uint8_t envelopeDecays[2];
uint8_t sweepDividers[2];
uint8_t pulseFlags[2];

uint8_t mirrorMode;
uint8_t mapperType;
uint8_t mapperRegister, mapperLatch, mapperShift;
uint8_t irqCounter, irqLatch;
bool irqEnable, irqReload;

uint8_t inputShift;

std::string romName;
uint8_t *rom;
uint32_t lastBankAddress, vromAddress;
bool save;

std::chrono::steady_clock::time_point timer;
uint32_t framebuffer[256 * 240];
uint32_t displayBuffer[256 * 240];
void *displayMutex;
float wavelengths[2];

const uint32_t palette[] =
{
    0x757575FF, 0x271B8FFF, 0x0000ABFF, 0x47009FFF,
    0x8F0077FF, 0xAB0013FF, 0xA70000FF, 0x7F0B00FF,
    0x432F00FF, 0x004700FF, 0x005100FF, 0x003F17FF,
    0x1B3F5FFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xBCBCBCFF, 0x0073EFFF, 0x233BEFFF, 0x8300F3FF,
    0xBF00BFFF, 0xE7005BFF, 0xDB2B00FF, 0xCB4F0FFF,
    0x8B7300FF, 0x009700FF, 0x00AB00FF, 0x00933BFF,
    0x00838BFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0x3FBFFFFF, 0x5F97FFFF, 0xA78BFDFF,
    0xF77BFFFF, 0xFF77B7FF, 0xFF7763FF, 0xFF9B3BFF,
    0xF3BF3FFF, 0x83D313FF, 0x4FDF4BFF, 0x58F898FF,
    0x00EBDBFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0xABE7FFFF, 0xC7D7FFFF, 0xD7CBFFFF,
    0xFFC7FFFF, 0xFFC7DBFF, 0xFFBFB3FF, 0xFFDBABFF,
    0xFFE7A3FF, 0xE3FFA3FF, 0xABF3BFFF, 0xB3FFFCFF,
    0x9FFFF3FF, 0x000000FF, 0x000000FF, 0x000000FF
};

const uint8_t noteLengths[] =
{
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

typedef struct
{
    void *pointer;
    uint32_t size;
} StateItem;

const std::vector<StateItem> stateItems =
{
    { cpuMemory,        sizeof(cpuMemory)        },
    { ppuMemory,        sizeof(ppuMemory)        },
    { sprMemory,        sizeof(sprMemory)        },
    { &globalCycles,    sizeof(globalCycles)     },
    { &cpuCycles,       sizeof(cpuCycles)        },
    { &cpuTargetCycles, sizeof(cpuTargetCycles)  },
    { &programCounter,  sizeof(programCounter)   },
    { &accumulator,     sizeof(accumulator)      },
    { &registerX,       sizeof(registerX)        },
    { &registerY,       sizeof(registerY)        },
    { &flags,           sizeof(flags)            },
    { &stackPointer,    sizeof(stackPointer)     },
    { interrupts,       sizeof(interrupts)       },
    { &scanline,        sizeof(scanline)         },
    { &scanlineCycles,  sizeof(scanlineCycles)   },
    { &ppuAddress,      sizeof(ppuAddress)       },
    { &ppuTempAddr,     sizeof(ppuTempAddr)      },
    { &ppuBuffer,       sizeof(ppuBuffer)        },
    { &scrollX,         sizeof(scrollX)          },
    { &spriteCount,     sizeof(spriteCount)      },
    { &ppuToggle,       sizeof(ppuToggle)        },
    { &frameCounter,    sizeof(frameCounter)     },
    { pulses,           sizeof(pulses)           },
    { lengthCounters,   sizeof(lengthCounters)   },
    { envelopeDividers, sizeof(envelopeDividers) },
    { envelopeDecays,   sizeof(envelopeDecays)   },
    { sweepDividers,    sizeof(sweepDividers)    },
    { pulseFlags,       sizeof(pulseFlags)       },
    { &mirrorMode,      sizeof(mirrorMode)       },
    { &mapperRegister,  sizeof(mapperRegister)   },
    { &mapperLatch,     sizeof(mapperLatch)      },
    { &mapperShift,     sizeof(mapperShift)      },
    { &irqCounter,      sizeof(irqCounter)       },
    { &irqLatch,        sizeof(irqLatch)         },
    { &irqEnable,       sizeof(irqEnable)        },
    { &irqReload,       sizeof(irqReload)        },
    { &inputShift,      sizeof(inputShift)       }
};

// Use the immediate value as a memory address
uint8_t *zeroPage()
{
    return &cpuMemory[cpuMemory[++programCounter]];
}

// Use the immediate value plus the X register as a memory address in zero page
uint8_t *zeroPageX()
{
    return &cpuMemory[(cpuMemory[++programCounter] + registerX) % 0x100];
}

// Use the immediate value plus the Y register as a memory address in zero page
uint8_t *zeroPageY()
{
    return &cpuMemory[(cpuMemory[++programCounter] + registerY) % 0x100];
}

// Use the immediate 2 values as a memory address
uint8_t *absolute()
{
    return &cpuMemory[cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8)];
}

// Use the absolute value plus the X register as a memory address
uint8_t *absoluteX(bool pageCycle)
{
    uint16_t address = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    if (pageCycle && (address & 0xFF) + registerX > 0xFF) // Page cross
        cpuTargetCycles++;
    return &cpuMemory[address + registerX];
}

// Use the absolute value plus the Y register as a memory address
uint8_t *absoluteY(bool pageCycle)
{
    uint16_t address = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    if (pageCycle && (address & 0xFF) + registerY > 0xFF) // Page cross
        cpuTargetCycles++;
    return &cpuMemory[address + registerY];
}

// Use the value stored at the absolute address as a memory address
uint8_t *indirect()
{
    uint16_t addressLower = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    uint16_t addressUpper = (addressLower & 0xFF00) | ((addressLower + 1) & 0x00FF);
    return &cpuMemory[(cpuMemory[addressUpper] << 8) | cpuMemory[addressLower]];
}

// Use the value stored at the zero page X address as a memory address
uint8_t *indirectX()
{
    uint8_t addressLower = cpuMemory[(cpuMemory[++programCounter] + registerX) % 0x100];
    uint8_t addressUpper = cpuMemory[(cpuMemory[programCounter] + registerX + 1) % 0x100];
    return &cpuMemory[(addressUpper << 8) | addressLower];
}

// Use the value stored at the zero page address plus the Y register as a memory address
uint8_t *indirectY(bool pageCycle)
{
    uint8_t addressLower = cpuMemory[cpuMemory[++programCounter]];
    uint8_t addressUpper = cpuMemory[(cpuMemory[programCounter] + 1) % 0x100];
    uint16_t address = (addressUpper << 8) | addressLower;
    if (pageCycle && (address & 0xFF) + registerY > 0xFF) // Page cross
        cpuTargetCycles++;
    return &cpuMemory[address + registerY];
}

// Get the value immediately after the current address
uint8_t *immediate()
{
    return &cpuMemory[++programCounter];
}

// Get the real location of a mirrored address in memory
uint16_t cpuMemoryMirror(uint16_t address)
{
    if (address >= 0x0800 && address < 0x2000)
        address %= 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + address % 8;
    return address;
}

// Get the real location of a mirrored address in PPU memory
uint16_t ppuMemoryMirror(uint16_t address)
{
    address %= 0x4000;
    if (address >= 0x3000 && address < 0x3F00)
        address -= 0x1000;
    else if (address >= 0x3F20 && address < 0x4000)
        address = 0x3F00 + address % 0x20;
    else if (address >= 0x3F10 && address < 0x3F20 && address % 4 == 0)
        address -= 0x10;

    // Nametable mirroring
    switch (mirrorMode)
    {
        case 0: // 1-screen A
            if (address >= 0x2400 && address < 0x3000)
                address -= ((address - 0x2000) / 0x0400) * 0x0400;
            break;

        case 1: // 1-screen B
            if (address >= 0x2000 && address < 0x2400)
                address += 0x0400;
            else if (address >= 0x2800 && address < 0x3000)
                address -= ((address - 0x2400) / 0x0400) * 0x0400;
            break;

        case 2: // Vertical
            if (address >= 0x2800 && address < 0x3000)
                address -= 0x0800;
            break;

        case 3: // Horizontal
            if (address >= 0x2400 && address < 0x2800)
                address -= 0x0400;
            else if (address >= 0x2800 && address < 0x3000)
                address -= ((address - 0x2400) / 0x0400) * 0x0400;
            break;
    }

    return address;
}

// Handle writes to mapper registers
void mapperWrite(uint16_t address, uint8_t value)
{
    switch (mapperType)
    {
        case 1: // MMC1
            if (value & 0x80)
            {
                // Reset the shift register on a write with bit 7 set
                mapperLatch |= 0x0C;
                mapperShift = 0;
            }
            else
            {
                // Write a bit to the latch
                mapperLatch |= (value & 0x01) << mapperShift++;
            }

            // Pass the 5-bit value to a register
            if (mapperShift == 5)
            {
                if (address >= 0x8000 && address < 0xA000) // Control
                {
                    mapperRegister = mapperLatch;
                    mirrorMode = mapperLatch & 0x03;
                }
                else if (address >= 0xA000 && address < 0xC000) // Swap VROM bank 0
                {
                    if (mapperRegister & 0x10) // 4 KB
                        memcpy(ppuMemory, &rom[vromAddress + 0x1000 * mapperLatch], 0x1000);
                    else // 8 KB
                        memcpy(ppuMemory, &rom[vromAddress + 0x1000 * (mapperLatch & ~0x01)], 0x2000);
                }
                else if (address >= 0xC000 && address < 0xE000) // Swap VROM bank 1
                {
                    if (mapperRegister & 0x10) // 4 KB
                        memcpy(&ppuMemory[0x1000], &rom[vromAddress + 0x1000 * mapperLatch], 0x1000);
                }
                else // Swap ROM banks
                {
                    if (mapperRegister & 0x04) // ROM bank 1 is fixed
                    {
                        if (mapperRegister & 0x08) // 16 KB
                            memcpy(&cpuMemory[0x8000], &rom[0x4000 * mapperLatch], 0x4000);
                        else // 32 KB
                            memcpy(&cpuMemory[0x8000], &rom[0x4000 * (mapperLatch & 0x1E)], 0x8000);

                        memcpy(&cpuMemory[0xC000], &rom[lastBankAddress], 0x4000);
                    }
                    else // ROM bank 0 is fixed
                    {
                        if (mapperRegister & 0x08) // 16 KB
                            memcpy(&cpuMemory[0xC000], &rom[0x4000 * mapperLatch], 0x4000);
                        else // 32 KB
                            memcpy(&cpuMemory[0x8000], &rom[0x4000 * (mapperLatch & 0x1E)], 0x8000);

                        memcpy(&cpuMemory[0x8000], rom, 0x4000);
                    }
                }

                mapperLatch = 0;
                mapperShift = 0;
            }

            break;

        case 2: // UNROM
            // Swap the first 16 KB ROM bank
            memcpy(&cpuMemory[0x8000], &rom[0x4000 * value], 0x4000);
            break;

        case 3: // CNROM
            // Swap the 8 KB VROM bank
            memcpy(ppuMemory, &rom[vromAddress + 0x2000 * (value & 0x03)], 0x2000);
            break;

        case 4: // MMC3
            if (address >= 0x8000 && address < 0xA000)
            {
                if (address % 2 == 0) // Select banks
                {
                    mapperRegister = value;
                    memcpy(&cpuMemory[(value & 0x40) ? 0x8000 : 0xC000], &rom[lastBankAddress], 0x2000);
                }
                else // Swap banks
                {
                    uint8_t bank = mapperRegister & 0x07;
                    if (bank < 2) // 2 KB VROM banks
                    {
                        memcpy(&ppuMemory[((mapperRegister & 0x80) << 5) + 0x0800 * bank],
                               &rom[vromAddress + 0x0400 * (value & ~0x01)], 0x0800);
                    }
                    else if (bank >= 2 && bank < 6) // 1 KB VROM banks
                    {
                        memcpy(&ppuMemory[(!(mapperRegister & 0x80) << 12) + 0x0400 * (bank - 2)],
                               &rom[vromAddress + 0x0400 * value], 0x0400);
                    }
                    else if (bank == 6) // Swappable/fixed 8 KB ROM bank
                    {
                        memcpy(&cpuMemory[(mapperRegister & 0x40) ? 0xC000 : 0x8000], &rom[0x2000 * value], 0x2000);
                    }
                    else // Swappable 8 KB ROM bank
                    {
                        memcpy(&cpuMemory[0xA000], &rom[0x2000 * value], 0x2000);
                    }
                }
            }
            else if (address >= 0xA000 && address < 0xC000)
            {
                if (address % 2 == 0) // Mirroring
                {
                    if (mirrorMode != 4)
                        mirrorMode = 2 + value;
                }
            }
            else if (address >= 0xC000 && address < 0xE000)
            {
                if (address % 2 == 0) // IRQ latch
                {
                    irqLatch = value;
                }
                else // IRQ reload
                {
                    irqCounter = 0;
                    irqReload = true;
                }
            }
            else // IRQ toggle
            {
                irqEnable = (address % 2 == 1);
            }
            break;
    }
}

// Clear a flag
void cl_(uint8_t flag)
{
    flags &= ~flag;
}

// Set a flag
void se_(uint8_t flag)
{
    flags |= flag;
}

// Push a value to the stack
void ph_(uint8_t value)
{
    cpuMemory[0x0100 + stackPointer--] = value;
}

// Pull the accumulator from the stack
void pla()
{
    accumulator = cpuMemory[0x0100 + ++stackPointer];

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Pull the flags from the stack
void plp()
{
    flags = cpuMemory[0x0100 + ++stackPointer];

    se_(0x20);
    cl_(0x10);
}

// Add with carry
void adc(uint8_t value)
{
    uint8_t oldAccum = accumulator;
    accumulator += value + (flags & 0x01);

    (accumulator & 0x80)                                                      ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)                                                        ? se_(0x02) : cl_(0x02); // Z
    ((value & 0x80) == (oldAccum & 0x80) && (flags & 0x80) != (value & 0x80)) ? se_(0x40) : cl_(0x40); // V
    (oldAccum > accumulator || value + (flags & 0x01) == 0x100)               ? se_(0x01) : cl_(0x01); // C
}

// Bitwise and
void _and(uint8_t value)
{
    accumulator &= value;

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Arithmetic shift left
void asl(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value <<= 1;

    (*value & 0x80)   ? se_(0x80) : cl_(0x80); // N
    (*value == 0)     ? se_(0x02) : cl_(0x02); // Z
    (oldValue & 0x80) ? se_(0x01) : cl_(0x01); // C
}

// Test bits
void bit(uint8_t value)
{
    (value & 0x80)               ? se_(0x80) : cl_(0x80); // N
    (value & 0x40)               ? se_(0x40) : cl_(0x40); // V
    ((accumulator & value) == 0) ? se_(0x02) : cl_(0x02); // Z
}

// Branch on condition
void b__(bool condition)
{
    int8_t value = *immediate();
    if (condition)
    {
        if ((programCounter & 0xFF) + value > 0xFF) // Page cross
            cpuTargetCycles++;
        programCounter += value;
        cpuTargetCycles++;
    }
}

// Break
void brk()
{
    if (!(flags & 0x04))
    {
        se_(0x10);
        interrupts[2] = true;
    }
    programCounter++;
}

// Compare a register
void cp_(uint8_t reg, uint8_t value)
{
    ((reg - value) & 0x80) ? se_(0x80) : cl_(0x80); // N
    (reg == value)         ? se_(0x02) : cl_(0x02); // Z
    (reg >= value)         ? se_(0x01) : cl_(0x01); // C
}

// Decrement a value
void de_(uint8_t *value)
{
    (*value)--;

    (*value & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*value == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Bitwise exclusive or
void eor(uint8_t value)
{
    accumulator ^= value;

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Increment a value
void in_(uint8_t *value)
{
    (*value)++;

    (*value & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*value == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Jump
void jmp(uint8_t *address)
{
    programCounter = address - cpuMemory - 1;
}

// Jump to subroutine
void jsr(uint8_t *address)
{
    ph_(programCounter >> 8);
    ph_(programCounter);
    programCounter = address - cpuMemory - 1;
}

// Load a register
void ld_(uint8_t *reg, uint8_t *value)
{
    uint16_t address = cpuMemoryMirror(value - cpuMemory);
    *reg = cpuMemory[address];

    (*reg & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*reg == 0)   ? se_(0x02) : cl_(0x02); // Z

    // Handle memory-mapped registers
    switch (address)
    {
        case 0x4015: // APUSTATUS
            // Set bits if the corresponding length counters are greater than 0
            *reg = 0;
            for (int i = 0; i < 2; i++)
                *reg |= (lengthCounters[i] > 0) << i;
            break;

        case 0x4016: // JOYPAD1
            // Read button status 1 bit at a time
            *reg = (cpuMemory[0x4016] & (1 << inputShift)) ? 0x41 : 0x40;
            ++inputShift %= 8;
            break;

        case 0x2002: // PPUSTATUS
            // Clear V-blank bit and write toggle
            cpuMemory[0x2002] &= ~0x80;
            ppuToggle = false;
            break;

        case 0x2004: // OAMDATA
            // Read from sprite memory
            *reg = sprMemory[cpuMemory[0x2003]];
            break;

        case 0x2007: // PPUDATA
            // Read from PPU memory, buffering non-palette reads
            *reg = (ppuAddress < 0x3F00) ? ppuBuffer : ppuMemory[ppuMemoryMirror(ppuAddress)];
            ppuBuffer = ppuMemory[ppuMemoryMirror(ppuAddress)];
            ppuAddress += (cpuMemory[0x2000] & 0x04) ? 32 : 1;
            break;
    }
}

// Logical shift right
void lsr(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value >>= 1;

    (*value & 0x80)   ? se_(0x80) : cl_(0x80); // N
    (*value == 0)     ? se_(0x02) : cl_(0x02); // Z
    (oldValue & 0x01) ? se_(0x01) : cl_(0x01); // C
}

// Bitwise or
void ora(uint8_t value)
{
    accumulator |= value;

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Transfer one register to another
void t__(uint8_t *src, uint8_t *dst)
{
    *dst = *src;

    (*dst & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*dst == 0)   ? se_(0x02) : cl_(0x02); // Z
}

// Transfer the X register to the stack pointer
void txs()
{
    stackPointer = registerX;
}

// Rotate left
void rol(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value = (*value << 1) | (flags & 0x01);

    (*value & 0x80)   ? se_(0x80) : cl_(0x80); // N
    (*value == 0)     ? se_(0x02) : cl_(0x02); // Z
    (oldValue & 0x80) ? se_(0x01) : cl_(0x01); // C
}

// Rotate right
void ror(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value = (*value >> 1) | ((flags & 0x01) << 7);

    (*value & 0x80)   ? se_(0x80) : cl_(0x80); // N
    (*value == 0)     ? se_(0x02) : cl_(0x02); // Z
    (oldValue & 0x01) ? se_(0x01) : cl_(0x01); // C
}

// Return from subroutine
void rts()
{
    programCounter = cpuMemory[0x0100 + ++stackPointer] | (cpuMemory[0x0100 + ++stackPointer] << 8);
}

// Return from interrupt
void rti()
{
    plp();
    rts();
    programCounter--;
}

// Subtract with carry
void sbc(uint8_t value)
{
    uint8_t oldAccum = accumulator;
    accumulator -= value + !(flags & 0x01);

    (accumulator & 0x80)                                                      ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)                                                        ? se_(0x02) : cl_(0x02); // Z
    ((value & 0x80) != (oldAccum & 0x80) && (flags & 0x80) == (value & 0x80)) ? se_(0x40) : cl_(0x40); // V
    (oldAccum >= accumulator && value + !(flags & 0x01) != 0x100)             ? se_(0x01) : cl_(0x01); // C
}

// Store a register
void st_(uint8_t reg, uint8_t *dst)
{
    uint16_t address = cpuMemoryMirror(dst - cpuMemory);

    if (address >= 0x8000)
        mapperWrite(address, reg);
    else if (address != 0x2002 && address != 0x4016)
        cpuMemory[address] = reg;

    // Handle memory-mapped registers
    switch (address)
    {
        case 0x4001: case 0x4005: // APU pulse channels
            // Set the sweep reload flag
            pulseFlags[(address - 0x4001) / 4] |= 0x02;
            break;

        case 0x4002: case 0x4006: // APU pulse channels
            pulses[(address - 0x4002) / 4] = ((cpuMemory[address + 1] & 0x07) << 8) | cpuMemory[address];
            break;

        case 0x4003: case 0x4007: // APU pulse channels
            // Load the pulse channel and set the envelope reload flag
            pulses[(address - 0x4003) / 4] = ((cpuMemory[address] & 0x07) << 8) | cpuMemory[address - 1];
            lengthCounters[(address - 0x4003) / 4] = noteLengths[(reg & 0xF8) >> 3];
            pulseFlags[(address - 0x4003) / 4] |= 0x01;
            break;

        case 0x4014: // OAMDMA
            // DMA transfer to sprite memory
            memcpy(sprMemory, &cpuMemory[cpuMemory[0x4014] * 0x100], 0x100);
            cpuTargetCycles += (globalCycles < 3) ? 513 : 514;
            break;

        case 0x4017: // APU frame counter
            frameCounter = 0;
            break;

        case 0x2000: // PPUCTRL
            // Select a nametable
            ppuTempAddr = (ppuTempAddr & ~0x0C00) | ((reg & 0x03) << 10);
            break;

        case 0x2004: // OAMDATA
            // Write a value to sprite memory
            sprMemory[cpuMemory[0x2003]] = reg;
            cpuMemory[0x2003]++;
            break;

        case 0x2005: // PPUSCROLL
            // Set the scroll positions
            ppuToggle = !ppuToggle;
            if (ppuToggle)
            {
                ppuTempAddr = (ppuTempAddr & ~0x001F) | ((reg & 0xF8) >> 3);
                scrollX = reg & 0x07;
            }
            else
            {
                ppuTempAddr = (ppuTempAddr & ~0x73E0) | ((reg & 0xF8) << 2) | ((reg & 0x07) << 12);
            }
            break;

        case 0x2006: // PPUADDR
            // Set the PPU address
            ppuToggle = !ppuToggle;
            if (ppuToggle)
                ppuTempAddr = (ppuTempAddr & ~0xFF00) | ((reg & 0x7F) << 8);
            else
                ppuAddress = ppuTempAddr = (ppuTempAddr & ~0x00FF) | reg;
            break;

        case 0x2007: // PPUDATA
            // Write a value to PPU memory
            ppuMemory[ppuMemoryMirror(ppuAddress)] = reg;
            ppuAddress += (cpuMemory[0x2000] & 0x04) ? 32 : 1;
            break;
    }
}

void cpu()
{
    // Only run on a CPU cycle (3 global cycles)
    if (globalCycles % 3 != 0)
        return;

    // Wait until the previous instruction's cycles have finished
    if (++cpuCycles < cpuTargetCycles)
        return;

    cpuCycles = cpuTargetCycles = 0;

    // Handle interrupts
    for (int i = 0; i < 3; i++)
    {
        if (interrupts[i])
        {
            ph_(programCounter >> 8);
            ph_(programCounter);
            ph_(flags);
            cl_(0x10);
            se_(0x04);
            programCounter = (cpuMemory[0xFFFB + i * 2] << 8) | cpuMemory[0xFFFA + i * 2];
            cpuTargetCycles += 7;
            interrupts[i] = false;
            return;
        }
    }

    // Decode opcode
    switch (cpuMemory[programCounter])
    {
        case 0x69: adc(*immediate());     cpuTargetCycles += 2; break; // ADC immediate
        case 0x65: adc(*zeroPage());      cpuTargetCycles += 3; break; // ADC zero page
        case 0x75: adc(*zeroPageX());     cpuTargetCycles += 4; break; // ADC zero page X
        case 0x6D: adc(*absolute());      cpuTargetCycles += 4; break; // ADC absolute
        case 0x7D: adc(*absoluteX(true)); cpuTargetCycles += 4; break; // ADC absolute X
        case 0x79: adc(*absoluteY(true)); cpuTargetCycles += 4; break; // ADC absolute Y
        case 0x61: adc(*indirectX());     cpuTargetCycles += 6; break; // ADC indirect X
        case 0x71: adc(*indirectY(true)); cpuTargetCycles += 5; break; // ADC indirect Y

        case 0x29: _and(*immediate());     cpuTargetCycles += 2; break; // AND immediate
        case 0x25: _and(*zeroPage());      cpuTargetCycles += 3; break; // AND zero page
        case 0x35: _and(*zeroPageX());     cpuTargetCycles += 4; break; // AND zero page X
        case 0x2D: _and(*absolute());      cpuTargetCycles += 4; break; // AND absolute
        case 0x3D: _and(*absoluteX(true)); cpuTargetCycles += 4; break; // AND absolute X
        case 0x39: _and(*absoluteY(true)); cpuTargetCycles += 4; break; // AND absolute Y
        case 0x21: _and(*indirectX());     cpuTargetCycles += 6; break; // AND indirect X
        case 0x31: _and(*indirectY(true)); cpuTargetCycles += 5; break; // AND indirect Y

        case 0x0A: asl(&accumulator);     cpuTargetCycles += 2; break; // ASL accumulator
        case 0x06: asl(zeroPage());       cpuTargetCycles += 5; break; // ASL zero page
        case 0x16: asl(zeroPageX());      cpuTargetCycles += 6; break; // ASL zero page X
        case 0x0E: asl(absolute());       cpuTargetCycles += 6; break; // ASL absolute
        case 0x1E: asl(absoluteX(false)); cpuTargetCycles += 7; break; // ASL absolute X

        case 0x24: bit(*zeroPage()); cpuTargetCycles += 3; break; // BIT zero page
        case 0x2C: bit(*absolute()); cpuTargetCycles += 4; break; // BIT absolute

        case 0x10: b__(!(flags & 0x80)); cpuTargetCycles += 2; break; // BPL
        case 0x30: b__( (flags & 0x80)); cpuTargetCycles += 2; break; // BMI
        case 0x50: b__(!(flags & 0x40)); cpuTargetCycles += 2; break; // BVC
        case 0x70: b__( (flags & 0x40)); cpuTargetCycles += 2; break; // BVS
        case 0x90: b__(!(flags & 0x01)); cpuTargetCycles += 2; break; // BCC
        case 0xB0: b__( (flags & 0x01)); cpuTargetCycles += 2; break; // BCS
        case 0xD0: b__(!(flags & 0x02)); cpuTargetCycles += 2; break; // BNE
        case 0xF0: b__( (flags & 0x02)); cpuTargetCycles += 2; break; // BEQ

        case 0x00: brk(); cpuTargetCycles += 7; break; // BRK

        case 0xC9: cp_(accumulator, *immediate());     cpuTargetCycles += 2; break; // CMP immediate
        case 0xC5: cp_(accumulator, *zeroPage());      cpuTargetCycles += 3; break; // CMP zero page
        case 0xD5: cp_(accumulator, *zeroPageX());     cpuTargetCycles += 4; break; // CMP zero page X
        case 0xCD: cp_(accumulator, *absolute());      cpuTargetCycles += 4; break; // CMP absolute
        case 0xDD: cp_(accumulator, *absoluteX(true)); cpuTargetCycles += 4; break; // CMP absolute X
        case 0xD9: cp_(accumulator, *absoluteY(true)); cpuTargetCycles += 4; break; // CMP absolute Y
        case 0xC1: cp_(accumulator, *indirectX());     cpuTargetCycles += 6; break; // CMP indirect X
        case 0xD1: cp_(accumulator, *indirectY(true)); cpuTargetCycles += 5; break; // CMP indirect Y

        case 0xE0: cp_(registerX, *immediate()); cpuTargetCycles += 2; break; // CPX immediate
        case 0xE4: cp_(registerX, *zeroPage());  cpuTargetCycles += 3; break; // CPX zero page
        case 0xEC: cp_(registerX, *absolute());  cpuTargetCycles += 4; break; // CPX absolute

        case 0xC0: cp_(registerY, *immediate()); cpuTargetCycles += 2; break; // CPY immediate
        case 0xC4: cp_(registerY, *zeroPage());  cpuTargetCycles += 3; break; // CPY zero page
        case 0xCC: cp_(registerY, *absolute());  cpuTargetCycles += 4; break; // CPY absolute

        case 0xC6: de_(zeroPage());       cpuTargetCycles += 5; break; // DEC zero page
        case 0xD6: de_(zeroPageX());      cpuTargetCycles += 6; break; // DEC zero page X
        case 0xCE: de_(absolute());       cpuTargetCycles += 6; break; // DEC absolute
        case 0xDE: de_(absoluteX(false)); cpuTargetCycles += 7; break; // DEC absolute X
        
        case 0x49: eor(*immediate());     cpuTargetCycles += 2; break; // EOR immediate
        case 0x45: eor(*zeroPage());      cpuTargetCycles += 3; break; // EOR zero page
        case 0x55: eor(*zeroPageX());     cpuTargetCycles += 4; break; // EOR zero page X
        case 0x4D: eor(*absolute());      cpuTargetCycles += 4; break; // EOR absolute
        case 0x5D: eor(*absoluteX(true)); cpuTargetCycles += 4; break; // EOR absolute X
        case 0x59: eor(*absoluteY(true)); cpuTargetCycles += 4; break; // EOR absolute Y
        case 0x41: eor(*indirectX());     cpuTargetCycles += 6; break; // EOR indirect X
        case 0x51: eor(*indirectY(true)); cpuTargetCycles += 5; break; // EOR indirect Y

        case 0x18: cl_(0x01); cpuTargetCycles += 2; break; // CLC
        case 0x38: se_(0x01); cpuTargetCycles += 2; break; // SEC
        case 0x58: cl_(0x04); cpuTargetCycles += 2; break; // CLI
        case 0x78: se_(0x04); cpuTargetCycles += 2; break; // SEI
        case 0xB8: cl_(0x40); cpuTargetCycles += 2; break; // CLV
        case 0xD8: cl_(0x08); cpuTargetCycles += 2; break; // CLD
        case 0xF8: se_(0x08); cpuTargetCycles += 2; break; // SED

        case 0xE6: in_(zeroPage());       cpuTargetCycles += 5; break; // INC zero page
        case 0xF6: in_(zeroPageX());      cpuTargetCycles += 6; break; // INC zero page X
        case 0xEE: in_(absolute());       cpuTargetCycles += 6; break; // INC absolute
        case 0xFE: in_(absoluteX(false)); cpuTargetCycles += 7; break; // INC absolute X

        case 0x4C: jmp(absolute()); cpuTargetCycles += 3; break; // JMP absolute
        case 0x6C: jmp(indirect()); cpuTargetCycles += 5; break; // JMP indirect

        case 0x20: jsr(absolute()); cpuTargetCycles += 6; break; // JSR absolute

        case 0xA9: ld_(&accumulator, immediate());     cpuTargetCycles += 2; break; // LDA immediate
        case 0xA5: ld_(&accumulator, zeroPage());      cpuTargetCycles += 3; break; // LDA zero page
        case 0xB5: ld_(&accumulator, zeroPageX());     cpuTargetCycles += 4; break; // LDA zero page X
        case 0xAD: ld_(&accumulator, absolute());      cpuTargetCycles += 4; break; // LDA absolute
        case 0xBD: ld_(&accumulator, absoluteX(true)); cpuTargetCycles += 4; break; // LDA absolute X
        case 0xB9: ld_(&accumulator, absoluteY(true)); cpuTargetCycles += 4; break; // LDA absolute Y
        case 0xA1: ld_(&accumulator, indirectX());     cpuTargetCycles += 6; break; // LDA indirect X
        case 0xB1: ld_(&accumulator, indirectY(true)); cpuTargetCycles += 5; break; // LDA indirect Y

        case 0xA2: ld_(&registerX, immediate());     cpuTargetCycles += 2; break; // LDX immediate
        case 0xA6: ld_(&registerX, zeroPage());      cpuTargetCycles += 2; break; // LDX zero page
        case 0xB6: ld_(&registerX, zeroPageY());     cpuTargetCycles += 2; break; // LDX zero page Y
        case 0xAE: ld_(&registerX, absolute());      cpuTargetCycles += 3; break; // LDX absolute
        case 0xBE: ld_(&registerX, absoluteY(true)); cpuTargetCycles += 3; break; // LDX absolute Y

        case 0xA0: ld_(&registerY, immediate());     cpuTargetCycles += 2; break; // LDY immediate
        case 0xA4: ld_(&registerY, zeroPage());      cpuTargetCycles += 2; break; // LDY zero page
        case 0xB4: ld_(&registerY, zeroPageX());     cpuTargetCycles += 2; break; // LDY zero page X
        case 0xAC: ld_(&registerY, absolute());      cpuTargetCycles += 3; break; // LDY absolute
        case 0xBC: ld_(&registerY, absoluteX(true)); cpuTargetCycles += 3; break; // LDY absolute X

        case 0x4A: lsr(&accumulator);     cpuTargetCycles += 2; break; // LSR accumulator
        case 0x46: lsr(zeroPage());       cpuTargetCycles += 5; break; // LSR zero page
        case 0x56: lsr(zeroPageX());      cpuTargetCycles += 6; break; // LSR zero page X
        case 0x4E: lsr(absolute());       cpuTargetCycles += 6; break; // LSR absolute
        case 0x5E: lsr(absoluteX(false)); cpuTargetCycles += 7; break; // LSR absolute X

        case 0xEA: cpuTargetCycles += 2; break; // NOP

        case 0x09: ora(*immediate());     cpuTargetCycles += 2; break; // ORA immediate
        case 0x05: ora(*zeroPage());      cpuTargetCycles += 3; break; // ORA zero page
        case 0x15: ora(*zeroPageX());     cpuTargetCycles += 4; break; // ORA zero page X
        case 0x0D: ora(*absolute());      cpuTargetCycles += 4; break; // ORA absolute
        case 0x1D: ora(*absoluteX(true)); cpuTargetCycles += 4; break; // ORA absolute X
        case 0x19: ora(*absoluteY(true)); cpuTargetCycles += 4; break; // ORA absolute Y
        case 0x01: ora(*indirectX());     cpuTargetCycles += 6; break; // ORA indirect X
        case 0x11: ora(*indirectY(true)); cpuTargetCycles += 5; break; // ORA indirect Y

        case 0xAA: t__(&accumulator, &registerX); cpuTargetCycles += 2; break; // TAX
        case 0x8A: t__(&registerX, &accumulator); cpuTargetCycles += 2; break; // TXA
        case 0xCA: de_(&registerX);               cpuTargetCycles += 2; break; // DEX
        case 0xE8: in_(&registerX);               cpuTargetCycles += 2; break; // INX
        case 0xA8: t__(&accumulator, &registerY); cpuTargetCycles += 2; break; // TAY
        case 0x98: t__(&registerY, &accumulator); cpuTargetCycles += 2; break; // TYA
        case 0x88: de_(&registerY);               cpuTargetCycles += 2; break; // DEY
        case 0xC8: in_(&registerY);               cpuTargetCycles += 2; break; // INY

        case 0x2A: rol(&accumulator);     cpuTargetCycles += 2; break; // ROL accumulator
        case 0x26: rol(zeroPage());       cpuTargetCycles += 5; break; // ROL zero page
        case 0x36: rol(zeroPageX());      cpuTargetCycles += 6; break; // ROL zero page X
        case 0x2E: rol(absolute());       cpuTargetCycles += 6; break; // ROL absolute
        case 0x3E: rol(absoluteX(false)); cpuTargetCycles += 7; break; // ROL absolute X

        case 0x6A: ror(&accumulator);     cpuTargetCycles += 2; break; // ROR accumulator
        case 0x66: ror(zeroPage());       cpuTargetCycles += 5; break; // ROR zero page
        case 0x76: ror(zeroPageX());      cpuTargetCycles += 6; break; // ROR zero page X
        case 0x6E: ror(absolute());       cpuTargetCycles += 6; break; // ROR absolute
        case 0x7E: ror(absoluteX(false)); cpuTargetCycles += 7; break; // ROR absolute X

        case 0x40: rti(); cpuTargetCycles += 6; break; // RTI

        case 0x60: rts(); cpuTargetCycles += 6; break; // RTS

        case 0xE9: sbc(*immediate());     cpuTargetCycles += 2; break; // SBC immediate
        case 0xE5: sbc(*zeroPage());      cpuTargetCycles += 3; break; // SBC zero page
        case 0xF5: sbc(*zeroPageX());     cpuTargetCycles += 4; break; // SBC zero page X
        case 0xED: sbc(*absolute());      cpuTargetCycles += 4; break; // SBC absolute
        case 0xFD: sbc(*absoluteX(true)); cpuTargetCycles += 4; break; // SBC absolute X
        case 0xF9: sbc(*absoluteY(true)); cpuTargetCycles += 4; break; // SBC absolute Y
        case 0xE1: sbc(*indirectX());     cpuTargetCycles += 6; break; // SBC indirect X
        case 0xF1: sbc(*indirectY(true)); cpuTargetCycles += 5; break; // SBC indirect Y

        case 0x85: st_(accumulator, zeroPage());       cpuTargetCycles += 3; break; // STA zero page
        case 0x95: st_(accumulator, zeroPageX());      cpuTargetCycles += 4; break; // STA zero page X
        case 0x8D: st_(accumulator, absolute());       cpuTargetCycles += 4; break; // STA absolute
        case 0x9D: st_(accumulator, absoluteX(false)); cpuTargetCycles += 5; break; // STA absolute X
        case 0x99: st_(accumulator, absoluteY(false)); cpuTargetCycles += 5; break; // STA absolute Y
        case 0x81: st_(accumulator, indirectX());      cpuTargetCycles += 6; break; // STA indirect X
        case 0x91: st_(accumulator, indirectY(false)); cpuTargetCycles += 6; break; // STA indirect Y

        case 0x9A: txs();                          cpuTargetCycles += 2; break; // TXS
        case 0xBA: t__(&stackPointer, &registerX); cpuTargetCycles += 2; break; // TSX
        case 0x48: ph_(accumulator);               cpuTargetCycles += 3; break; // PHA
        case 0x68: pla();                          cpuTargetCycles += 4; break; // PLA
        case 0x08: ph_(flags | 0x10);              cpuTargetCycles += 3; break; // PHP
        case 0x28: plp();                          cpuTargetCycles += 4; break; // PLP

        case 0x86: st_(registerX, zeroPage());  cpuTargetCycles += 3; break; // STX zero page
        case 0x96: st_(registerX, zeroPageY()); cpuTargetCycles += 4; break; // STX zero page Y
        case 0x8E: st_(registerX, absolute());  cpuTargetCycles += 4; break; // STX absolute

        case 0x84: st_(registerY, zeroPage());  cpuTargetCycles += 3; break; // STY zero page
        case 0x94: st_(registerY, zeroPageX()); cpuTargetCycles += 4; break; // STY zero page X
        case 0x8C: st_(registerY, absolute());  cpuTargetCycles += 4; break; // STY absolute

        default: printf("Unknown opcode: 0x%X\n", cpuMemory[programCounter]); break;
    }

    programCounter++;
}

void ppu()
{
    if (scanline >= 0 && scanline < 240) // Visible lines
    {
        if (scanlineCycles >= 1 && scanlineCycles <= 256 && cpuMemory[0x2001] & 0x08) // Background drawing
        {
            uint8_t x = scanlineCycles - 1;
            uint8_t y = scanline;
            uint16_t xOffset = x + ((ppuAddress & 0x001F) << 3) + scrollX;
            uint16_t yOffset = ((ppuAddress & 0x03E0) >> 2) + (ppuAddress >> 12);
            uint16_t tableOffset = 0x2000 | (ppuAddress & 0x0C00);

            if (xOffset >= 256)
            {
                tableOffset ^= 0x0400;
                xOffset %= 256;
            }
            tableOffset = ppuMemoryMirror(tableOffset);

            // Get the lower 2 bits of the palette index
            uint16_t patternOffset = (cpuMemory[0x2000] & 0x10) << 8;
            uint16_t tile = patternOffset + ppuMemory[tableOffset + (yOffset / 8) * 32 + xOffset / 8] * 16;
            uint8_t lowerBits = ppuMemory[tile + yOffset % 8] & (0x80 >> (xOffset % 8)) ? 0x01 : 0x00;
            lowerBits |= ppuMemory[tile + yOffset % 8 + 8] & (0x80 >> (xOffset % 8)) ? 0x02 : 0x00;

            if ((x >= 8 || cpuMemory[0x2001] & 0x02) && lowerBits != 0)
            {
                // Get the upper 2 bits of the palette index from the attribute table
                uint8_t upperBits = ppuMemory[tableOffset + 0x03C0 + (yOffset / 32) * 8 + xOffset / 32];
                if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 0) // Top left
                    upperBits = (upperBits & 0x03) << 2;
                else if ((xOffset / 16) % 2 == 1 && (yOffset / 16) % 2 == 0) // Top right
                    upperBits = (upperBits & 0x0C) << 0;
                else if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 1) // Bottom left
                    upperBits = (upperBits & 0x30) >> 2;
                else // Bottom right
                    upperBits = (upperBits & 0xC0) >> 4;

                uint8_t type = framebuffer[y * 256 + x];

                // Check for a sprite 0 hit
                if (type < 0xFD)
                    cpuMemory[0x2002] |= 0x40;

                // Draw a pixel
                if (type % 2 == 1)
                    framebuffer[y * 256 + x] = palette[ppuMemory[0x3F00 | upperBits | lowerBits]];
            }

            // Increment the Y coordinate at the end of the scanline
            if (scanlineCycles == 256)
            {
                if ((ppuAddress & 0x7000) == 0x7000)
                {
                    if ((ppuAddress & 0x03E0) == 0x03A0)
                        ppuAddress = (ppuAddress & ~0x73E0) ^ 0x0800;
                    else
                        ppuAddress = (ppuAddress & ~0x73E0) | ((ppuAddress + 0x0020) & 0x03E0);
                }
                else
                {
                    ppuAddress += 0x1000;
                }
            }
        }
        else if (scanlineCycles >= 257 && scanlineCycles <= 320 && cpuMemory[0x2001] & 0x10) // Sprite drawing
        {
            uint8_t *sprite = &sprMemory[(scanlineCycles - 257) * 4];
            uint8_t height = (cpuMemory[0x2000] & 0x20) ? 16 : 8;
            uint8_t y = scanline;

            if (*sprite <= y && *sprite + height > y)
            {
                if (spriteCount < 8)
                {
                    uint8_t x = *(sprite + 3);
                    uint8_t spriteY = ((y - *sprite) / 8) * 16 + (y - *sprite) % 8;
                    uint16_t patternOffset = (height == 8) ? (cpuMemory[0x2000] & 0x08) << 9 : (*(sprite + 1) & 0x01) << 12;
                    uint16_t tile = patternOffset + (*(sprite + 1) & (height == 8 ? ~0x00 : ~0x01)) * 16;
                    uint8_t upperBits = (*(sprite + 2) & 0x03) << 2;

                    if (*(sprite + 2) & 0x80)
                    {
                        // Flip the sprite vertically
                        if (height == 16 && spriteY < 8)
                            tile += 16;
                        spriteY = 7 - (spriteY % 8);
                    }

                    y++;

                    // Draw a sprite line on the next scanline
                    for (int i = 0; i < 8; i++)
                    {
                        uint16_t xOffset = x + ((*(sprite + 2) & 0x40) ? 7 - i : i);
                        uint8_t lowerBits = ppuMemory[tile + spriteY] & (0x80 >> i) ? 0x01 : 0x00;
                        lowerBits |= ppuMemory[tile + spriteY + 8] & (0x80 >> i) ? 0x02 : 0x00;

                        if (xOffset < 256 && (xOffset >= 8 || cpuMemory[0x2001] & 0x04) && lowerBits != 0)
                        {
                            uint8_t type = framebuffer[y * 256 + xOffset];
                            if (type == 0xFF)
                            {
                                framebuffer[y * 256 + xOffset] = palette[ppuMemory[0x3F10 | upperBits | lowerBits]];

                                // Mark opaque pixels
                                framebuffer[y * 256 + xOffset]--;
                                if (*(sprite + 2) & 0x20) // Sprite is behind the background
                                    framebuffer[y * 256 + xOffset]--;
                            }
                            if (scanlineCycles == 257) // Sprite 0
                                framebuffer[y * 256 + xOffset] -= 2;
                        }
                    }

                    if (!disableSpriteLimit)
                        spriteCount++;
                }
                else // Sprite overflow
                {
                    cpuMemory[0x2002] |= 0x20;
                }
            }

            // Set the horizontal scroll data
            if (scanlineCycles == 257 && cpuMemory[0x2001] & 0x18)
                ppuAddress = (ppuAddress & ~0x041F) | (ppuTempAddr & 0x041F);
        }

        // MMC3 IRQ counter
        if (mapperType == 4 && scanlineCycles == 260 && cpuMemory[0x2001] & 0x18)
        {
            if (irqCounter == 0)
            {
                // Trigger an IRQ if they're enabled
                if (irqEnable && !irqReload)
                    interrupts[2] = true;

                // Reload the counter
                irqCounter = irqLatch;
                irqReload = false;
            }
            else
            {
                irqCounter--;
            }
        }
    }
    else if (scanline == 241 && scanlineCycles == 1) // Start of the V-blank period
    {
        cpuMemory[0x2002] |= 0x80;
        if (cpuMemory[0x2000] & 0x80)
            interrupts[0] = true;
    }
    else if (scanline == 261) // Pre-render line
    {
        // Clear the bits for the next frame
        if (scanlineCycles == 1)
            cpuMemory[0x2002] &= ~0xE0;

        // Set the scroll data
        if (cpuMemory[0x2001] & 0x18)
        {
            if (scanlineCycles == 257)
                ppuAddress = (ppuAddress & ~0x041F) | (ppuTempAddr & 0x041F);
            else if (scanlineCycles >= 280 && scanlineCycles <= 304)
                ppuAddress = (ppuAddress & ~0x7BE0) | (ppuTempAddr & 0x7BE0);
        }
    }

    // Update the scanline counters
    if (++scanlineCycles == 341)
    {
        spriteCount = 0;
        scanlineCycles = 0;
        scanline++;
    }

    if (scanline == 262) // End of a frame
    {
        scanline = 0;

        // Copy the finished frame to the display
        lockMutex(displayMutex);
        memcpy(displayBuffer, framebuffer, sizeof(displayBuffer));
        unlockMutex(displayMutex);

        // Clear the framebuffer
        for (int i = 0; i < 256 * 240; i++)
            framebuffer[i] = palette[ppuMemory[0x3F00]];

        // Limit the FPS to 60 if enabled
        if (frameLimiter)
        {
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - timer;
            if (elapsed.count() < 1.0f / 60)
                usleep((1.0f / 60 - elapsed.count()) * 1000000);
            timer = std::chrono::steady_clock::now();
        }
    }
}

void apu()
{
    // Only run on an APU cycle (6 global cycles)
    if (globalCycles % 6 != 0)
        return;

    // Advance the frame counter
    if (++frameCounter == 3729 || frameCounter == 7457 || frameCounter == 11186 ||
       (!(cpuMemory[0x4017] & 0x80) && frameCounter == 14915) || frameCounter == 18641)
    {
        for (int i = 0; i < 2; i++)
        {
            if (pulseFlags[i] & 0x01)
            {
                // Reload the envelope values
                pulseFlags[i] &= ~0x01;
                envelopeDividers[i] = cpuMemory[0x4000 + 4 * i] & 0x0F;
                envelopeDecays[i] = 0x0F;
            }
            else
            {
                // Clock the dividers
                if (envelopeDividers[i] == 0)
                {
                    envelopeDividers[i] = cpuMemory[0x4000 + 4 * i] & 0x0F;
                    if (envelopeDecays[i] != 0)
                        envelopeDecays[i]--;
                    else if (cpuMemory[0x4000 + 4 * i] & 0x20) // Loop flag
                        envelopeDecays[i] = 0x0F;
                }
                else
                {
                    envelopeDividers[i]--;
                }
            }

            if (frameCounter == 7457 || frameCounter == 14915 || frameCounter == 18641) // Half frame
            {
                // Clock the length counters if they're enabled
                if (!(cpuMemory[0x4000 + 4 * i] & 0x20))
                {
                    if (lengthCounters[i] == 0)
                        pulses[i] = 0;
                    else
                        lengthCounters[i]--;
                }

                // Sweep the pulse channel frequencies if sweeps are enabled
                if (sweepDividers[i] == 0 && cpuMemory[0x4001 + i * 4] & 0x80)
                {
                    int16_t sweep = ((cpuMemory[0x4003 + 4 * i] & 0x07) << 8) | cpuMemory[0x4002 + 4 * i];
                    sweep >>= cpuMemory[0x4001 + i * 4] & 0x07;
                    if (cpuMemory[0x4001 + i * 4] & 0x08) // Negation
                        sweep -= 2 * sweep + ((i == 0) ? 1 : 0);
                    pulses[i] += sweep;
                }

                // Clock the sweep dividers
                if ((pulseFlags[i] & 0x02) || sweepDividers[i] == 0)
                {
                    pulseFlags[i] &= ~0x02;
                    sweepDividers[i] = (cpuMemory[0x4001 + i * 4] & 0x70) >> 4;
                }
                else
                {
                    sweepDividers[i]--;
                }
            }
        }

        if (frameCounter == 14915 || frameCounter == 18641)
            frameCounter = 0;
    }

    // Update the pulse channel information
    for (int i = 0; i < 2; i++)
    {
        if (cpuMemory[0x4015] & (1 << i))
        {
            dutyCycles[i] = (cpuMemory[0x4000 + 4 * i] & 0xC0) >> 6;
            if (cpuMemory[0x4000 + 4 * i] & 0x10) // Constant volume
                volumes[i] = cpuMemory[0x4000 + 4 * i] & 0x0F;
            else // Envelope-controlled volume
                volumes[i] = envelopeDecays[i];

            if (pulses[i] < 8 || pulses[i] > 0x7FF || lengthCounters[i] == 0)
                pulses[i] = 0;
        }
        else
        {
            pulses[i] = lengthCounters[i] = 0;
        }
    }
}

bool loadRom(std::string filename)
{
    FILE *romFile = fopen(filename.c_str(), "rb");
    if (!romFile)
    {
        printf("Failed to open ROM!\n");
        return false;
    }

    // Clear the state items
    for (unsigned int i = 0; i < stateItems.size(); i++)
        memset(stateItems[i].pointer, 0, stateItems[i].size);

    // Read the ROM header
    uint8_t header[0x10];
    fread(header, 1, 0x10, romFile);
    mirrorMode = (header[6] & 0x08) ? 4 : 2 + !(header[6] & 0x01);
    mapperType = header[7] | (header[6] >> 4);

    // Verify the file format
    char filetype[4];
    memcpy(filetype, header, 3);
    filetype[3] = '\0';
    if (strcmp(filetype, "NES") != 0 || header[3] != 0x1A)
    {
        printf("Invalid ROM format!\n");
        return false;
    }

    romName = filename.substr(0, filename.rfind("."));

    // Load a savefile if the ROM supports it
    if (header[6] & 0x02)
    {
        save = true;
        FILE *saveFile = fopen((romName + ".sav").c_str(), "rb");
        if (saveFile)
        {
            fread(&cpuMemory[0x6000], 1, 0x2000, saveFile);
            fclose(saveFile);
        }
    }

    // Load the ROM trainer into memory if it exists
    if (header[6] & 0x04)
        fread(&cpuMemory[0x7000], 1, 0x200, romFile);

    // Move the ROM into its own memory
    uint16_t romStart = ftell(romFile);
    fseek(romFile, 0, SEEK_END);
    uint32_t romEnd = ftell(romFile);
    rom = new uint8_t[romEnd - romStart];
    fseek(romFile, romStart, SEEK_SET);
    fread(rom, 1, romEnd - romStart, romFile);
    fclose(romFile);

    // Load the ROM banks into memory
    switch (mapperType)
    {
        case 0: // NROM
        case 3: // CNROM
            // Mirror a single 16 KB bank or load both 16 KB banks
            memcpy(&cpuMemory[0x8000], rom, 0x4000);
            if (header[4] == 1)
                memcpy(&cpuMemory[0xC000], &cpuMemory[0x8000], 0x4000);
            else
                memcpy(&cpuMemory[0xC000], &rom[0x4000], 0x4000);
            break;

        case 1: // MMC1
        case 2: // UNROM
        case 4: // MMC3
            // Load the first and last 16 KB banks
            lastBankAddress = (header[4] - 1) * 0x4000;
            memcpy(&cpuMemory[0x8000], rom, 0x4000);
            memcpy(&cpuMemory[0xC000], &rom[lastBankAddress], 0x4000);
            mapperRegister = 0x04;
            break;

        default:
            printf("Unknown mapper type: %d\n", mapperType);
            return false;
    }

    // Load the first 8 KB of VROM into PPU memory if it exists
    if (header[5])
    {
        vromAddress = header[4] * 0x4000;
        memcpy(ppuMemory, &rom[vromAddress], 0x2000);
    }

    // Trigger the reset interrupt
    interrupts[1] = true;

    displayMutex = createMutex();

    return true;
}

void closeRom()
{
    // Write the savefile
    if (save)
    {
        FILE *saveFile = fopen((romName + ".sav").c_str(), "wb");
        fwrite(&cpuMemory[0x6000], 1, 0x2000, saveFile);
        fclose(saveFile);
    }
}

void runCycle()
{
    cpu();
    ppu();
    apu();
    ++globalCycles %= 6;
}

int16_t audioSample(float pitch)
{
    int16_t out = 0;

    // Generate the pulse waves
    for (int i = 0; i < 2; i++)
    {
        wavelengths[i] += pitch;

        if ((dutyCycles[i] == 0 && wavelengths[i] <  pulses[i] / 8) ||
            (dutyCycles[i] == 1 && wavelengths[i] <  pulses[i] / 4) ||
            (dutyCycles[i] == 2 && wavelengths[i] <  pulses[i] / 2) ||
            (dutyCycles[i] == 3 && wavelengths[i] >= pulses[i] / 4))
            out += 0x200 * volumes[i];

        if (wavelengths[i] >= pulses[i])
            wavelengths[i] = 0;
    }

    return out;
}

void pressKey(uint8_t key)
{
    // Set the bit corresponding to the key
    cpuMemory[0x4016] |= 1 << key;
}

void releaseKey(uint8_t key)
{
    // Clear the bit corresponding to the key
    cpuMemory[0x4016] &= ~(1 << key);
}

void saveState()
{
    FILE *state = fopen((romName + ".noi").c_str(), "wb");

    // Write the state items to a state file
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fwrite(stateItems[i].pointer, 1, stateItems[i].size, state);

    fclose(state);
}

void loadState()
{
    FILE *state = fopen((romName + ".noi").c_str(), "rb");
    if (!state)
        return;

    // Load the state items from the state file
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fread(stateItems[i].pointer, 1, stateItems[i].size, state);

    fclose(state);
}
