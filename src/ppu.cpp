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
#include "cpu.h"
#include "mapper.h"
#include "mutex.h"

namespace ppu
{

chrono::steady_clock::time_point timer;
uint32_t framebuffer[256 * 240];
uint32_t displayBuffer[256 * 240];
void *displayMutex;

uint8_t memory[0x4000];
uint8_t sprMemory[0x100];
uint8_t pixelBuffer[0x10];

uint16_t scanline, scanlineDot;
uint16_t ppuAddress, ppuTempAddr;
uint8_t scrollX;
uint8_t control, mask, status;
uint8_t oamAddress;
uint8_t readBuffer;
uint8_t spriteCount;
bool writeToggle;

uint8_t mirrorMode;

const vector<core::StateItem> stateItems =
{
    { memory,       sizeof(memory)      },
    { sprMemory,    sizeof(sprMemory)   },
    { &scanline,    sizeof(scanline)    },
    { &scanlineDot, sizeof(scanlineDot) },
    { &ppuAddress,  sizeof(ppuAddress)  },
    { &ppuTempAddr, sizeof(ppuTempAddr) },
    { &scrollX,     sizeof(scrollX)     },
    { &control,     sizeof(control)     },
    { &mask,        sizeof(mask)        },
    { &status,      sizeof(status)      },
    { &oamAddress,  sizeof(oamAddress)  },
    { &readBuffer,  sizeof(readBuffer)  },
    { &spriteCount, sizeof(spriteCount) },
    { &writeToggle, sizeof(writeToggle) }
};

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

void reset()
{
    // Clear the state items
    for (unsigned int i = 0; i < stateItems.size(); i++)
        memset(stateItems[i].pointer, 0, stateItems[i].size);

    displayMutex = mutex::create();
}

uint16_t memoryMirror(uint16_t address)
{
    // Get the real location of a mirrored address in memory
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

void fetchPixels()
{
    uint16_t xOffset = ((ppuAddress & 0x001F) << 3);
    uint16_t yOffset = ((ppuAddress & 0x03E0) >> 2) + (ppuAddress >> 12);
    uint16_t tableOffset = memoryMirror(0x2000 | (ppuAddress & 0x0C00));
    uint16_t tile = ((control & 0x10) << 8) + memory[tableOffset + (yOffset / 8) * 32 + xOffset / 8] * 16;

    // Get the upper 2 bits of the palette index from the attribute table
    uint8_t upperBits = memory[tableOffset + 0x03C0 + (yOffset / 32) * 8 + xOffset / 32];
    if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 0) // Top left
        upperBits = (upperBits & 0x03) << 2;
    else if ((xOffset / 16) % 2 == 1 && (yOffset / 16) % 2 == 0) // Top right
        upperBits = (upperBits & 0x0C) << 0;
    else if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 1) // Bottom left
        upperBits = (upperBits & 0x30) >> 2;
    else // Bottom right
        upperBits = (upperBits & 0xC0) >> 4;

    for (int i = 0; i < 8; i++)
    {
        // Get the lower 2 bits of the palette index from the pattern table
        uint8_t lowerBits = memory[tile + yOffset % 8] & (0x80 >> ((xOffset + i) % 8)) ? 0x01 : 0x00;
        lowerBits |= memory[tile + yOffset % 8 + 8] & (0x80 >> ((xOffset + i) % 8)) ? 0x02 : 0x00;

        // Shift the pixel buffer and store the new data
        pixelBuffer[i] = pixelBuffer[i + 8];
        pixelBuffer[i + 8] = upperBits | lowerBits;
    }

    // Set the MMC2 latches
    if (tile == 0x1FD0)
        mapper::mmc2SetLatch(1, false);
    else if (tile == 0x1FE0)
        mapper::mmc2SetLatch(1, true);

    // Increment the coarse X coordinate 
    if ((ppuAddress & 0x001F) == 0x1F)
        ppuAddress = (ppuAddress & ~0x001F) ^ 0x0400;
    else
        ppuAddress++;
}

void runCycle()
{
    if (scanline < 240 && (mask & 0x18)) // Visible lines
    {
        if (scanlineDot >= 1 && scanlineDot <= 256 && (mask & 0x08)) // Background drawing
        {
            uint8_t x = scanlineDot - 1;

            // Update the pixel buffer
            if (x != 0 && x % 8 == 0)
                fetchPixels();
            uint8_t color = pixelBuffer[x % 8 + scrollX];

            if ((x >= 8 || (mask & 0x02)) && (color & 0x03) != 0)
            {
                uint32_t *pixel = &framebuffer[scanline * 256 + x];
                uint8_t type = *pixel;

                // Check for a sprite 0 hit
                if (type < 0xFD)
                    status |= 0x40;

                // Draw a pixel
                if (type % 2 == 1)
                    *pixel = palette[memory[0x3F00 | color]];
            }
        }
        else if (scanlineDot >= 257 && scanlineDot <= 320 && (mask & 0x10)) // Sprite drawing
        {
            uint8_t *sprite = &sprMemory[(scanlineDot - 257) * 4];
            uint8_t height = (control & 0x20) ? 16 : 8;
            uint8_t y = scanline;

            if (*sprite <= y && *sprite + height > y)
            {
                if (spriteCount < 8)
                {
                    uint8_t spriteX = *(sprite + 3);
                    uint8_t spriteY = ((y - *sprite) / 8) * 16 + (y - *sprite) % 8;
                    uint8_t upperBits = (*(sprite + 2) & 0x03) << 2;

                    uint16_t tile;
                    if (height == 8)
                        tile = ((control & 0x08) << 9) + *(sprite + 1) * 16;
                    else
                        tile = ((*(sprite + 1) & 0x01) << 12) + (*(sprite + 1) & ~0x01) * 16;

                    // Flip the sprite vertically if needed
                    if (*(sprite + 2) & 0x80)
                    {
                        if (height == 16 && spriteY < 8)
                            tile += 16;
                        spriteY = 7 - (spriteY % 8);
                    }

                    // Draw a sprite line on the next scanline
                    y++;
                    for (int i = 0; i < 8; i++)
                    {
                        uint16_t xOffset = spriteX + ((*(sprite + 2) & 0x40) ? 7 - i : i);
                        uint8_t lowerBits = memory[tile + spriteY] & (0x80 >> i) ? 0x01 : 0x00;
                        lowerBits |= memory[tile + spriteY + 8] & (0x80 >> i) ? 0x02 : 0x00;

                        if ((xOffset >= 8 || (mask & 0x04)) && xOffset < 256 && lowerBits != 0)
                        {
                            uint32_t *pixel = &framebuffer[y * 256 + xOffset];
                            uint8_t type = *pixel;

                            if (type == 0xFF)
                            {
                                // Draw a pixel
                                *pixel = palette[memory[0x3F10 | upperBits | lowerBits]];

                                // Mark opaque pixels
                                (*pixel)--;
                                if (*(sprite + 2) & 0x20) // Behind background
                                    (*pixel)--;
                            }

                            // Mark sprite 0
                            if (scanlineDot == 257)
                                *pixel -= 2;
                        }
                    }

                    // Set the MMC2 latches
                    if (tile == 0x0FD0)
                        mapper::mmc2SetLatch(0, false);
                    else if (tile == 0x0FE0)
                        mapper::mmc2SetLatch(0, true);

                    if (!config::disableSpriteLimit)
                        spriteCount++;
                }
                else
                {
                    // Set the sprite overflow flag
                    status |= 0x20;
                }
            }
        }

        if (scanlineDot == 256)
        {
            // Increment the Y coordinate
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
    else if (scanline == 241 && scanlineDot == 1) // Start of V-blank
    {
        // Trigger an NMI if enabled
        status |= 0x80;
        if (control & 0x80)
            cpu::interrupts[0] = true;
    }
    else if (scanline == 261) // Pre-render line
    {
        // Clear the bits for the next frame
        if (scanlineDot == 1)
            status &= ~0xE0;

        // Reload the vertical scroll data if rendering is enabled
        if (scanlineDot >= 280 && scanlineDot <= 304 && (mask & 0x18))
            ppuAddress = (ppuAddress & ~0x7BE0) | (ppuTempAddr & 0x7BE0);
    }

    if ((scanline < 240 || scanline == 261) && (mask & 0x18))
    {
        // Perform updates that happen on both visible lines and the pre-render line
        if (scanlineDot == 257) // Horizontal scroll data
            ppuAddress = (ppuAddress & ~0x041F) | (ppuTempAddr & 0x041F);
        else if (scanlineDot == 260) // MMC3 IRQ counter
            mapper::mmc3Counter();
        else if (scanlineDot == 328 || scanlineDot == 336) // Pixel buffer
            fetchPixels();
    }

    // Update the scanline position
    scanlineDot++;
    if (scanlineDot == 341) // End of scanline
    {
        scanlineDot = spriteCount = 0;
        scanline++;

        if (scanline == 262) // End of frame
        {
            // Copy the finished frame to the display
            mutex::lock(displayMutex);
            memcpy(displayBuffer, framebuffer, sizeof(displayBuffer));
            mutex::unlock(displayMutex);

            // Clear the framebuffer
            for (int i = 0; i < 256 * 240; i++)
                framebuffer[i] = palette[memory[0x3F00]];

            // Limit the FPS to 60 if enabled
            if (config::frameLimiter)
            {
                chrono::duration<double> elapsed = chrono::steady_clock::now() - timer;
                if (elapsed.count() < 1.0f / 60)
                    usleep((1.0f / 60 - elapsed.count()) * 1000000);
                timer = chrono::steady_clock::now();
            }

            scanline = 0;
        }
    }
}

uint8_t registerRead(uint16_t address)
{
    uint8_t value = 0;

    // Handle reads from memory-mapped registers
    switch (address)
    {
        case 0x2002: // PPUSTATUS
            // Clear the V-blank bit and write toggle
            value = status;
            status &= ~0x80;
            writeToggle = false;
            break;

        case 0x2004: // OAMDATA
            // Read from sprite memory
            value = sprMemory[oamAddress];
            break;

        case 0x2007: // PPUDATA
            // Read from PPU memory, buffering non-palette reads
            value = (ppuAddress < 0x3F00) ? readBuffer : memory[memoryMirror(ppuAddress)];
            readBuffer = memory[memoryMirror(ppuAddress)];
            ppuAddress += (control & 0x04) ? 32 : 1;
            break;
    }

    return value;
}

void registerWrite(uint16_t address, uint8_t value)
{
    // Handle writes to memory-mapped registers
    switch (address)
    {
        case 0x2000: // PPUCTRL
            control = value;
            ppuTempAddr = (ppuTempAddr & ~0x0C00) | ((value & 0x03) << 10);
            break;

        case 0x2001: // PPUMASK
            mask = value;
            break;

        case 0x2003: // OAMADDR
            oamAddress = value;
            break;

        case 0x2004: // OAMDATA
            // Write a value to sprite memory
            sprMemory[oamAddress] = value;
            oamAddress++;
            break;

        case 0x2005: // PPUSCROLL
            // Set the scroll positions
            writeToggle = !writeToggle;
            if (writeToggle)
            {
                ppuTempAddr = (ppuTempAddr & ~0x001F) | ((value & 0xF8) >> 3);
                scrollX = value & 0x07;
            }
            else
            {
                ppuTempAddr = (ppuTempAddr & ~0x73E0) | ((value & 0xF8) << 2) | ((value & 0x07) << 12);
            }
            break;

        case 0x2006: // PPUADDR
            // Set the PPU address
            writeToggle = !writeToggle;
            if (writeToggle)
                ppuTempAddr = (ppuTempAddr & ~0xFF00) | ((value & 0x7F) << 8);
            else
                ppuAddress = ppuTempAddr = (ppuTempAddr & ~0x00FF) | value;
            break;

        case 0x2007: // PPUDATA
            // Write a value to PPU memory
            memory[memoryMirror(ppuAddress)] = (ppuAddress < 0x3F00) ? value : value % 0x40;
            ppuAddress += (control & 0x04) ? 32 : 1;
            break;

        case 0x4014: // OAMDMA
            // DMA transfer to sprite memory
            memcpy(sprMemory, &cpu::memory[value * 0x100], 0x100);
            break;
    }
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
