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
#include "../config.h"
#include "../mutex.h"

int outSamples;
AudioOutBuffer audioBuffer, *audioReleasedBuffer;

const u32 defaultKeyMap[] = { KEY_A, KEY_B, KEY_MINUS, KEY_PLUS, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L | KEY_R };

bool paused;

const vector<string> controlNames =
{
    "A Button",
    "B Button",
    "Select Button",
    "Start Button",
    "D-Pad Up",
    "D-Pad Down",
    "D-Pad Left",
    "D-Pad Right",
    "Reset to Defaults"
};

const vector<string> controlValueNames =
{
    "Default",
    "A Button", "B Button", "X Button", "Y Button",
    "Left Stick Click", "Right Stick Click",
    "L Button", "R Button", "ZL Button", "ZR Button",
    "Plus Button", "Minus Button",
    "D-Pad Left", "D-Pad Up", "D-Pad Right", "D-Pad Down",
    "Left Stick Left", "Left Stick Up", "Left Stick Right", "Left Stick Down",
    "Right Stick Left", "Right Stick Up", "Right Stick Right", "Right Stick Down"
};

const vector<Value> controlValues =
{
    { controlValueNames, &keyMap[0] },
    { controlValueNames, &keyMap[1] },
    { controlValueNames, &keyMap[2] },
    { controlValueNames, &keyMap[3] },
    { controlValueNames, &keyMap[4] },
    { controlValueNames, &keyMap[5] },
    { controlValueNames, &keyMap[6] },
    { controlValueNames, &keyMap[7] }
};

const vector<string> settingNames =
{
    "Disable Sprite Limit",
    "Screen Filtering",
    "Frame Limiter"
};

const vector<Value> settingValues =
{
    { { "Off", "On" }, &disableSpriteLimit },
    { { "Off", "On" }, &screenFiltering    },
    { { "Off", "On" }, &frameLimiter       }
};

const vector<string> pauseNames =
{
    "Resume",
    "Save State",
    "Load State",
    "Settings",
    "File Browser"
};

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
    setTextureFiltering(screenFiltering);
    Thread core, audio;
    threadCreate(&core, runCore, NULL, 0x80000, 0x30, 1);
    threadStart(&core);
    threadCreate(&audio, audioOutput, NULL, 0x80000, 0x30, 2);
    threadStart(&audio);
}

void controlsMenu()
{
    int selection = 0;

    while (true)
    {
        u32 pressed = menuScreen("Controls", "", "", {}, controlNames, controlValues, &selection);

        if (pressed & KEY_A)
        {
            if (selection == (int)controlNames.size() - 1) // Reset to defaults
            {
                for (unsigned int i = 0; i < controlValues.size(); i++)
                    *controlValues[i].value = 0;
            }
            else
            {
                pressed = messageScreen("Controls", {"Press a button to map it to: " + controlNames[selection]}, false);
                for (unsigned int i = 0; i < controlValueNames.size(); i++)
                {
                    if (pressed & BIT(i))
                        *controlValues[selection].value = i + 1;
                }
            }
        }
        else if (pressed & KEY_B)
        {
            return;
        }
    }
}

void settingsMenu()
{
    int selection = 0;

    while (true)
    {
        u32 pressed = menuScreen("Settings", "", "Controls", {}, settingNames, settingValues, &selection);

        if (pressed & KEY_A)
        {
            *settingValues[selection].value = !(*settingValues[selection].value);
        }
        else if (pressed & KEY_B)
        {
            saveConfig();
            return;
        }
        else if (pressed & KEY_X)
        {
            controlsMenu();
        }
    }
}

bool fileBrowser()
{
    string romPath = "sdmc:/";
    int selection = 0;

    while (true)
    {
        vector<string> files = dirContents(romPath, ".nes");
        u32 pressed = menuScreen("NoiES", "Exit", "Settings", {}, files, {}, &selection);

        if (pressed & KEY_A && files.size() > 0)
        {
            romPath += "/" + files[selection];
            selection = 0;

            if (romPath.find(".nes", romPath.length() - 4) != string::npos)
            {
                if (!loadRom(romPath))
                {
                    vector<string> message =
                    {
                        "The ROM couldn't be loaded.",
                        "It probably either has an unsupported mapper or is corrupt."
                    };

                    messageScreen("Unable to load ROM", message, true);
                    return false;
                }
                break;
            }
        }
        else if (pressed & KEY_B && romPath != "sdmc:/")
        {
            romPath = romPath.substr(0, romPath.rfind("/"));
            selection = 0;
        }
        else if (pressed & KEY_X)
        {
            settingsMenu();
        }
        else if (pressed & KEY_PLUS)
        {
            return false;
        }
    }

    return true;
}

bool pauseMenu()
{
    paused = true;
    int selection = 0;

    while (paused)
    {
        u32 pressed = menuScreen("NoiES", "", "", {}, pauseNames, {}, &selection);

        if (pressed & KEY_A)
        {
            if (selection == 1) // Save State
            {
                saveState();
            }
            else if (selection == 2) // Load State
            {
                loadState();
            }
            else if (selection == 3) // Settings
            {
                settingsMenu();
            }
            else if (selection == 4) // File Browser
            {
                if (!fileBrowser())
                    return false;
            }
        }

        if ((pressed & KEY_A && selection != 3) || pressed & KEY_B)
            startCore();
    }

    return true;
}

int main(int argc, char **argv)
{
    initRenderer();
    loadConfig();

    if (!fileBrowser())
    {
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
            if (pressed & ((keyMap[i] == 0) ? defaultKeyMap[i] : BIT(keyMap[i] - 1)))
                pressKey(i);
            else if (released & ((keyMap[i] == 0) ? defaultKeyMap[i] : BIT(keyMap[i] - 1)))
                releaseKey(i);
        }

        if (pressed & defaultKeyMap[8])
        {
            if (!pauseMenu())
                break;
        }

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
