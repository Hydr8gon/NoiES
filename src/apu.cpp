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

float wavelengths[2];
uint16_t frequencies[2];
uint16_t baseFrequencies[2];
uint8_t lengthCounters[2];
uint8_t envelopePeriods[2];
uint8_t envelopeDividers[2];
uint8_t envelopeDecays[2];
uint8_t sweepPeriods[2];
uint8_t sweepDividers[2];
uint8_t sweepShifts[2];
uint8_t dutyCycles[2];
uint8_t pulseFlags[2];

uint16_t frameCounter;
uint8_t frameCounterFlags;
uint8_t status;

const uint8_t noteLengths[] =
{
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const vector<core::StateItem> stateItems =
{
    { wavelengths,        sizeof(wavelengths)       },
    { frequencies,        sizeof(frequencies)       },
    { baseFrequencies,    sizeof(baseFrequencies)   },
    { lengthCounters,     sizeof(lengthCounters)    },
    { envelopePeriods,    sizeof(envelopePeriods)   },
    { envelopeDividers,   sizeof(envelopeDividers)  },
    { envelopeDecays,     sizeof(envelopeDecays)    },
    { sweepPeriods,       sizeof(sweepPeriods)      },
    { sweepDividers,      sizeof(sweepDividers)     },
    { sweepShifts,        sizeof(sweepShifts)       },
    { dutyCycles,         sizeof(dutyCycles)        },
    { pulseFlags,         sizeof(pulseFlags)        },
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
        wavelengths[i] += pitch;

        if ((dutyCycles[i] == 0 && wavelengths[i] <  frequencies[i] / 8) ||
            (dutyCycles[i] == 1 && wavelengths[i] <  frequencies[i] / 4) ||
            (dutyCycles[i] == 2 && wavelengths[i] <  frequencies[i] / 2) ||
            (dutyCycles[i] == 3 && wavelengths[i] >= frequencies[i] / 4))
            out += 0x200 * ((pulseFlags[i] & 0x10) ? envelopePeriods[i] : envelopeDecays[i]);

        if (wavelengths[i] >= frequencies[i])
            wavelengths[i] = 0;
    }

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
    for (int i = 0; i < 2; i++)
    {
        // Clock the pulse envelopes
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
}

void halfFrame()
{
    for (int i = 0; i < 2; i++)
    {
        // Clock the length counters if they're not halted
        if (!(pulseFlags[i] & 0x20))
        {
            if (lengthCounters[i] != 0)
                lengthCounters[i]--;
        }

        // Clock the pulse sweep dividers
        if (sweepDividers[i] == 0 || (pulseFlags[i] & 0x02))
        {
            // Sweep the frequencies if sweeps are enabled
            if (sweepDividers[i] == 0 && (pulseFlags[i] & 0x80))
            {
                int16_t sweep = baseFrequencies[i] >> sweepShifts[i];
                if (pulseFlags[i] & 0x08) // Negation
                    sweep -= 2 * sweep + !i;
                frequencies[i] += sweep;
            }

            sweepDividers[i] = sweepPeriods[i];
            pulseFlags[i] &= ~0x02;
        }
        else
        {
            sweepDividers[i]--;
        }
    }
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

    // Check if a channel should be silenced
    for (int i = 0; i < 2; i++)
    {
        if (frequencies[i] < 8 || frequencies[i] > 0x7FF || lengthCounters[i] == 0)
            frequencies[i] = 0;
        if (!(status & (1 << i)))
            frequencies[i] = lengthCounters[i] = 0;
    }
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
                value |= (lengthCounters[i] > 0) << i;
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
            frequencies[i] = baseFrequencies[i] = (baseFrequencies[i] & 0x700) | value;
            break;

        case 0x4003: case 0x4007: // Pulse channels
            lengthCounters[i] = noteLengths[(value & 0xF8) >> 3];
            frequencies[i] = baseFrequencies[i] = ((value & 0x07) << 8) | (baseFrequencies[i] & 0x0FF);
            wavelengths[i] = 0;
            pulseFlags[i] |= 0x01; // Envelope reload
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
