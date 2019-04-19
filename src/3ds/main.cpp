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

#include <3ds.h>
#include <cstring>

#include "../core.h"
#include "../ppu.h"
#include "../apu.h"
#include "../config.h"
#include "../mutex.h"

bool running = true;
bool requestSave, requestLoad;

u32 keyMap[] = { KEY_A, KEY_B, KEY_SELECT, KEY_START, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L, KEY_R, KEY_TOUCH };
string romPath = "sdmc:/3ds/noies/game.nes";

const vector<config::Setting> platformSettings =
{
    { "keyA",      &keyMap[0],  false },
    { "keyB",      &keyMap[1],  false },
    { "keySelect", &keyMap[2],  false },
    { "keyStart",  &keyMap[3],  false },
    { "keyUp",     &keyMap[4],  false },
    { "keyDown",   &keyMap[5],  false },
    { "keyLeft",   &keyMap[6],  false },
    { "keyRight",  &keyMap[7],  false },
    { "keySave",   &keyMap[8],  false },
    { "keyLoad",   &keyMap[9],  false },
    { "keyExit",   &keyMap[10], false },
    { "romPath",   &romPath,    true  }
};

void runCore(void *args)
{
    while (running)
    {
        core::runCycle();

        if (requestSave)
        {
            core::saveState();
            requestSave = false;
        }
        else if (requestLoad)
        {
            core::loadState();
            requestLoad = false;
        }
    }
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    u8 *framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    consoleInit(GFX_BOTTOM, NULL);

    config::load(platformSettings);

    if (core::loadRom(romPath) != 0)
    {
        printf("The current ROM path is: %s\n", romPath.c_str());
        printf("Press any button to exit.\n");
        u32 pressed;
        while (!pressed)
        {
            hidScanInput();
            pressed = hidKeysDown();
        }
        config::save();
        return 1;
    }

    u8 model;
    cfguInit();
    CFGU_GetSystemModel(&model);
    cfguExit();

    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, 48000);
    float mix[] = { 1.0f, 1.0f };
    ndspChnSetMix(0, mix);

    u8 currentBuf = 0;
    ndspWaveBuf waveBuffers[2];
    for (int i = 0; i < 2; i++)
    {
        memset(&waveBuffers[i], 0, sizeof(waveBuffers[i]));
        waveBuffers[i].data_vaddr = linearAlloc(1600 * 2 * sizeof(s16));
        waveBuffers[i].nsamples = 1600;
        ndspChnWaveBufAdd(0, &waveBuffers[i]);
    }

    Thread core;
    if (model > 1 && model != 3)
    {
        ptmSysmInit();
        PTMSYSM_ConfigureNew3DSCPU(0x03);
        ptmSysmExit();
        core = threadCreate(runCore, NULL, 0x8000, 0x30, 2, false);
    }
    else
    {
        APT_SetAppCpuTimeLimit(30);
        core = threadCreate(runCore, NULL, 0x8000, 0x30, 1, false);
    }

    while (aptMainLoop())
    {
        hidScanInput();
        u32 pressed = hidKeysDown();
        u32 released = hidKeysUp();

        for (int i = 0; i < 8; i++)
        {
            if (pressed & keyMap[i])
                core::pressKey(i);
            else if (released & keyMap[i])
                core::releaseKey(i);
        }

        if (pressed & keyMap[8]) // Save state
            requestSave = true;
        else if (pressed & keyMap[9]) // Load state
            requestLoad = true;
        else if (pressed & keyMap[10]) // Exit
            break;

        if (waveBuffers[currentBuf].status == NDSP_WBUF_DONE)
        {
            for (unsigned int i = 0; i < waveBuffers[currentBuf].nsamples; i++)
            {
                s16 sample = apu::audioSample(2.3f);
                waveBuffers[currentBuf].data_pcm16[i * 2]     = sample;
                waveBuffers[currentBuf].data_pcm16[i * 2 + 1] = sample;
            }
            ndspChnWaveBufAdd(0, &waveBuffers[currentBuf]);
            currentBuf = !currentBuf;
        }

        mutex::lock(ppu::displayMutex);
        for (int y = 0; y < 240; y++)
        {
            for (int x = 0; x < 256; x++)
            {
                framebuffer[((x + 72) * 240 + 239 - y) * 3]     = ppu::displayBuffer[y * 256 + x] >>  8;
                framebuffer[((x + 72) * 240 + 239 - y) * 3 + 1] = ppu::displayBuffer[y * 256 + x] >> 16;
                framebuffer[((x + 72) * 240 + 239 - y) * 3 + 2] = ppu::displayBuffer[y * 256 + x] >> 24;
            }
        }
        mutex::unlock(ppu::displayMutex);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    running = false;
    threadJoin(core, U64_MAX);
    threadFree(core);
    config::save();
    ndspExit();
    gfxExit();
    return 0;
}
