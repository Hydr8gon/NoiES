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

#include "ui.h"
#include "../core.h"
#include "../mutex.h"

string romPath = "sdmc:/";

int outSamples;
AudioOutBuffer audioBuffer, *audioReleasedBuffer;

bool paused;
const vector<string> pauseNames = { "Resume", "Save State", "Load State" };

const u32 keymap[] = { KEY_A, KEY_B, KEY_MINUS, KEY_PLUS, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L | KEY_R };

void setupAudioBuffer()
{
    // Dynamically switch audio sample rate when the system is docked/undocked
    // For some reason both modes sound best with different sample rates
    outSamples = (appletGetOperationMode() == AppletOperationMode_Handheld) ? 1440 : 2048;
    audioBuffer.next = NULL;
    audioBuffer.buffer = new s16[(outSamples + 0xFFF) & ~0xFFF];
    audioBuffer.buffer_size = (outSamples * sizeof(s16) + 0xFFF) & ~0xFFF;
    audioBuffer.data_size = outSamples * sizeof(s16);
    audioBuffer.data_offset = 0;
}

void onAppletHook(AppletHookType hook, void *param)
{
    if (hook == AppletHookType_OnOperationMode || hook == AppletHookType_OnPerformanceMode)
        setupAudioBuffer();
}

void runCore(void *args)
{
    while (!paused)
        runCycle();
}

void audioOutput(void *args)
{
    while (!paused)
    {
        for (int i = 0; i < outSamples; i++)
            ((s16*)audioBuffer.buffer)[i] = audioSample(1.15f);
        audoutPlayBuffer(&audioBuffer, &audioReleasedBuffer);
    }
}

void startCore()
{
    paused = false;
    setTextureFiltering(false);
    Thread core, audio;
    threadCreate(&core, runCore, NULL, 0x80000, 0x30, 1);
    threadStart(&core);
    threadCreate(&audio, audioOutput, NULL, 0x80000, 0x30, 2);
    threadStart(&audio);
}

bool fileBrowser()
{
    int selection = 0;

    while (true)
    {
        vector<string> files = dirContents(romPath, ".nes");
        u32 pressed = menuScreen("NoiES", "Exit", "", {}, files, {}, &selection);

        if (pressed & KEY_A && files.size() > 0)
        {
            romPath += "/" + files[selection];
            selection = 0;

            if (romPath.find(".nes", romPath.length() - 4) != string::npos)
                break;
        }
        else if (pressed & KEY_B && romPath != "sdmc:/")
        {
            romPath = romPath.substr(0, romPath.rfind("/"));
            selection = 0;
        }
        else if (pressed & KEY_PLUS)
        {
            return false;
        }
    }

    return true;
}

void pauseMenu()
{
    paused = true;

    int selection = 0;

    while (paused)
    {
        u32 pressed = menuScreen("NoiES", "", "", {}, pauseNames, {}, &selection);

        if (pressed & KEY_A)
        {
            if (selection == 1) // Save state
                saveState();
            else if (selection == 2) // Load state
                loadState();
        }

        if (pressed & (KEY_A | KEY_B))
            startCore();
    }
}

int main(int argc, char **argv)
{
    initRenderer();

    if (!fileBrowser())
    {
        deinitRenderer();
        return 0;
    }

    if (!loadRom(romPath))
    {
        vector<string> message =
        {
            "The ROM couldn't be loaded.",
            "It probably either has an unsupported mapper or is corrupt."
        };

        messageScreen("Unable to load ROM", message, true);
        deinitRenderer();
        return 0;
    }

    appletLockExit();
    AppletHookCookie cookie;
    appletHook(&cookie, onAppletHook, NULL);
    audoutInitialize();
    audoutStartAudioOut();
    setupAudioBuffer();

    startCore();

    while (appletMainLoop())
    {
        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 released = hidKeysUp(CONTROLLER_P1_AUTO);

        for (int i = 0; i < 8; i++)
        {
            if (pressed & keymap[i])
                pressKey(i);
            else if (released & keymap[i])
                releaseKey(i);
        }

        if (pressed & keymap[8])
            pauseMenu();

        clearDisplay(0);
        lockMutex(displayMutex);
        drawTexture(displayBuffer, 256, 240, 0, false, 256, 0, 768, 720);
        unlockMutex(displayMutex);
        refreshDisplay();
    }

    closeRom();

    audoutStopAudioOut();
    audoutExit();
    appletUnhook(&cookie);
    appletUnlockExit();
    deinitRenderer();
    return 0;
}
