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

#include <cstring>
#include <vector>

#include "core.h"
#include "cpu.h"

namespace apu
{

float pulseWaves[2];
uint16_t pulseFreqs[2];
uint16_t pulseBaseFreqs[2];
uint8_t pulseLengths[2];
uint8_t envelopePeriods[2];
uint8_t envelopeDividers[2];
uint8_t envelopeDecays[2];
uint8_t sweepPeriods[2];
uint8_t sweepDividers[2];
uint8_t sweepShifts[2];
uint8_t dutyCycles[2];
uint8_t pulseFlags[2];

float triangleWave;
uint16_t triangleFreq;
uint16_t triangleBaseFreq;
uint8_t triangleLength;
uint8_t linearCounter;
uint8_t linearReload;
uint8_t triangleFlags;

uint16_t frameCounter;
uint8_t frameCounterFlags;
uint8_t status;

const uint8_t noteLengths[] =
{
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const uint8_t triangleSteps[] =
{
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

const vector<core::StateItem> stateItems =
{
    { pulseWaves,         sizeof(pulseWaves)        },
    { pulseFreqs,         sizeof(pulseFreqs)        },
    { pulseBaseFreqs,     sizeof(pulseBaseFreqs)    },
    { pulseLengths,       sizeof(pulseLengths)      },
    { envelopePeriods,    sizeof(envelopePeriods)   },
    { envelopeDividers,   sizeof(envelopeDividers)  },
    { envelopeDecays,     sizeof(envelopeDecays)    },
    { sweepPeriods,       sizeof(sweepPeriods)      },
    { sweepDividers,      sizeof(sweepDividers)     },
    { sweepShifts,        sizeof(sweepShifts)       },
    { dutyCycles,         sizeof(dutyCycles)        },
    { pulseFlags,         sizeof(pulseFlags)        },
    { &triangleWave,      sizeof(triangleWave)      },
    { &triangleFreq,      sizeof(triangleFreq)      },
    { &triangleBaseFreq,  sizeof(triangleBaseFreq)  },
    { &triangleLength,    sizeof(triangleLength)    },
    { &linearCounter,     sizeof(linearCounter)     },
    { &linearReload,      sizeof(linearReload)      },
    { &triangleFlags,     sizeof(triangleFlags)     },
    { &frameCounter,      sizeof(frameCounter)      },
    { &frameCounterFlags, sizeof(frameCounterFlags) },
    { &status,            sizeof(status)            }
};

int16_t audioSample(float pitch)
{
    int16_t out = 0;

    // Generate the pulse waves
    for (int i = 0; i < 2; i++)
    {
        pulseWaves[i] += pitch;
        if (pulseWaves[i] >= pulseFreqs[i])
            pulseWaves[i] = 0;

        if ((dutyCycles[i] == 0 && pulseWaves[i] <  pulseFreqs[i] / 8) ||
            (dutyCycles[i] == 1 && pulseWaves[i] <  pulseFreqs[i] / 4) ||
            (dutyCycles[i] == 2 && pulseWaves[i] <  pulseFreqs[i] / 2) ||
            (dutyCycles[i] == 3 && pulseWaves[i] >= pulseFreqs[i] / 4))
            out += 0x200 * ((pulseFlags[i] & 0x10) ? envelopePeriods[i] : envelopeDecays[i]);
    }

    // Generate the triangle wave
    if (triangleLength != 0 && linearCounter != 0)
        triangleWave += pitch / 2;
    if (triangleWave >= triangleFreq + 1)
        triangleWave = 0;
    uint8_t step = (triangleWave / (triangleFreq + 1)) * 32;
    out += 0x243 * triangleSteps[step];

    return out;
}

void reset()
{
    // Clear the state items
    for (unsigned int i = 0; i < stateItems.size(); i++)
        memset(stateItems[i].pointer, 0, stateItems[i].size);
}

void quarterFrame()
{
    // Clock the pulse envelopes
    for (int i = 0; i < 2; i++)
    {
        if (pulseFlags[i] & 0x01)
        {
            // Reload the divider and decay values
            envelopeDividers[i] = envelopePeriods[i];
            envelopeDecays[i] = 0x0F;
            pulseFlags[i] &= ~0x01;
        }
        else
        {
            // Clock the dividers
            if (envelopeDividers[i] == 0)
            {
                // Decay and reload the divider
                if (envelopeDecays[i] != 0)
                    envelopeDecays[i]--;
                else if (pulseFlags[i] & 0x20) // Loop flag
                    envelopeDecays[i] = 0x0F;
                envelopeDividers[i] = envelopePeriods[i];
            }
            else
            {
                envelopeDividers[i]--;
            }
        }
    }

    // Clock the triangle linear counter
    if (triangleFlags & 0x01) // Linear counter reload
        linearCounter = linearReload;
    else if (linearCounter != 0)
        linearCounter--;
    if (!(triangleFlags & 0x80)) // Control flag
        triangleFlags &= ~0x01;
}

void halfFrame()
{
    // Clock the pulse length counters and sweeps
    for (int i = 0; i < 2; i++)
    {
        // Clock the length counters if they're not halted
        if (!(pulseFlags[i] & 0x20) && pulseLengths[i] != 0)
            pulseLengths[i]--;

        // Clock the sweep dividers
        if (sweepDividers[i] == 0 || (pulseFlags[i] & 0x02))
        {
            // Sweep the frequencies if sweeps are enabled
            if (sweepDividers[i] == 0 && (pulseFlags[i] & 0x80))
            {
                int16_t sweep = pulseBaseFreqs[i] >> sweepShifts[i];
                if (pulseFlags[i] & 0x08) // Negation
                    sweep -= 2 * sweep + !i;
                pulseFreqs[i] += sweep;
            }

            sweepDividers[i] = sweepPeriods[i];
            pulseFlags[i] &= ~0x02;
        }
        else
        {
            sweepDividers[i]--;
        }
    }

    // Clock the triangle length counter if it's not halted
    if (!(triangleFlags & 0x80) && triangleLength != 0)
        triangleLength--;
}

void runCycle()
{
    // Only run on an APU cycle (6 global cycles)
    if (core::globalCycles % 6 != 0)
        return;

    // Advance the frame counter
    frameCounter++;
    if (frameCounter == 3729 || frameCounter == 7457 || frameCounter == 11186 ||
        frameCounter == ((frameCounterFlags & 0x80) ? 18641 : 14915))
    {
        quarterFrame();
        if (frameCounter != 3729 && frameCounter != 11186)
            halfFrame();

        // Trigger an optional IRQ at the end of the 4-step sequence
        if (frameCounter == 14915 && !(frameCounterFlags & 0x40))
        {
            status |= 0x40;
            cpu::interrupts[2] = true;
        }

        if (frameCounter == 14915 || frameCounter == 18641)
            frameCounter = 0;
    }

    // Check if either of the pulse channels should be silenced
    for (int i = 0; i < 2; i++)
    {
        if (!(status & (1 << i)))
            pulseLengths[i] = 0;
        if (pulseFreqs[i] < 8 || pulseFreqs[i] > 0x7FF || pulseLengths[i] == 0)
            pulseFreqs[i] = 0;
    }

    // Check if the triangle channel should be silenced
    if (!(status & 0x04))
        triangleLength = 0;
}

uint8_t registerRead(uint16_t address)
{
    uint8_t value = 0;

    switch (address)
    {
        case 0x4015: // APU status
            // Set bits if the corresponding length counters are greater than 0
            value = status & 0xC0;
            for (int i = 0; i < 2; i++)
                value |= (pulseLengths[i] > 0) << i;
            value |= (triangleLength > 0) << 2;
            status &= ~0x40;
            break;
    }

    return value;
}

void registerWrite(uint16_t address, uint8_t value)
{
    int i = (address - 0x4000) / 4;

    switch (address)
    {
        case 0x4000: case 0x4004: // Pulse channels
            dutyCycles[i] = (value & 0xC0) >> 6;
            pulseFlags[i] = (pulseFlags[i] & ~0x20) | (value & 0x20); // Length counter halt
            pulseFlags[i] = (pulseFlags[i] & ~0x10) | (value & 0x10); // Constant volume
            envelopePeriods[i] = value & 0x0F;
            break;

        case 0x4001: case 0x4005: // Pulse channels
            sweepPeriods[i] = (value & 0x70) >> 4;
            pulseFlags[i] = (pulseFlags[i] & ~0x80) | (value & 0x80); // Sweep enable
            pulseFlags[i] = (pulseFlags[i] & ~0x08) | (value & 0x08); // Sweep negate
            sweepShifts[i] = value & 0x07;
            pulseFlags[i] |= 0x02; // Sweep reload
            break;

        case 0x4002: case 0x4006: // Pulse channels
            pulseFreqs[i] = pulseBaseFreqs[i] = (pulseBaseFreqs[i] & 0x700) | value;
            break;

        case 0x4003: case 0x4007: // Pulse channels
            pulseLengths[i] = noteLengths[(value & 0xF8) >> 3];
            pulseFreqs[i] = pulseBaseFreqs[i] = ((value & 0x07) << 8) | (pulseBaseFreqs[i] & 0x0FF);
            pulseWaves[i] = 0;
            pulseFlags[i] |= 0x01; // Envelope reload
            break;

        case 0x4008: // Triangle channel
            triangleFlags = (triangleFlags & ~0x80) | (value & 0x80); // Length counter halt
            linearReload = value & 0x7F;
            triangleFlags |= 0x01; // Linear counter reload
            break;

        case 0x400A: // Triangle channel
            triangleFreq = triangleBaseFreq = (triangleBaseFreq & 0x700) | value;
            break;

        case 0x400B: // Triangle channel
            triangleLength = noteLengths[(value & 0xF8) >> 3];
            triangleFreq = triangleBaseFreq = ((value & 0x07) << 8) | (triangleBaseFreq & 0x0FF);
            triangleFlags |= 0x01; // Linear counter reload
            break;

        case 0x4015: // Status
            // Set the channel enable bits without clearing the interrupt bits
            status = (status & 0xC0) | (value & 0x1F);
            break;

        case 0x4017: // Frame counter
            frameCounterFlags = value;
            if (frameCounterFlags & 0x40) // Interrupt inhibit
                status &= ~0x40; // Frame interrupt
            frameCounter = 0;
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
