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
#include "../ppu.h"
#include "../apu.h"
#include "../config.h"
#include "../mutex.h"

bool paused;

AudioOutBuffer *audioBuffer;
u32 count;

const u32 defaultKeyMap[] = { KEY_A, KEY_B, KEY_MINUS, KEY_PLUS, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L | KEY_R };

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
    { controlValueNames, &config::keyMap[0] },
    { controlValueNames, &config::keyMap[1] },
    { controlValueNames, &config::keyMap[2] },
    { controlValueNames, &config::keyMap[3] },
    { controlValueNames, &config::keyMap[4] },
    { controlValueNames, &config::keyMap[5] },
    { controlValueNames, &config::keyMap[6] },
    { controlValueNames, &config::keyMap[7] }
};

const vector<string> settingNames =
{
    "Disable Sprite Limit",
    "Screen Filtering",
    "Frame Limiter"
};

const vector<Value> settingValues =
{
    { { "Off", "On" }, &config::disableSpriteLimit },
    { { "Off", "On" }, &config::screenFiltering    },
    { { "Off", "On" }, &config::frameLimiter       }
};

const vector<string> pauseNames =
{
    "Resume",
    "Save State",
    "Load State",
    "Settings",
    "File Browser"
};

void runCore(void *args)
{
    while (!paused)
        core::runCycle();
}

void audioOutput(void *args)
{
    while (!paused)
    {
        audoutWaitPlayFinish(&audioBuffer, &count, U64_MAX);
        for (int i = 0; i < 1024; i++)
        {
            s16 sample = apu::audioSample(2.3f);
            ((s16*)audioBuffer->buffer)[i * 2]     = sample;
            ((s16*)audioBuffer->buffer)[i * 2 + 1] = sample;
        }
        audoutAppendAudioOutBuffer(audioBuffer);
    }
}

void startCore()
{
    paused = false;
    appletLockExit();
    audoutInitialize();
    audoutStartAudioOut();
    setupAudioBuffer();
    setTextureFiltering(config::screenFiltering);
    Thread core, audio;
    threadCreate(&core, runCore, NULL, 0x80000, 0x30, 1);
    threadStart(&core);
    threadCreate(&audio, audioOutput, NULL, 0x80000, 0x30, 2);
    threadStart(&audio);
}

void stopCore()
{
    paused = true;
    audoutStopAudioOut();
    audoutExit();
    appletUnlockExit();
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
            config::save();
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
                int result = core::loadRom(romPath);
                if (result != 0)
                {
                    vector<string> message = { "The ROM couldn't be loaded." };
                    if (result > 2)
                        message.push_back("It uses an unsupported mapper: " + to_string(result));
                    else
                        message.push_back("The file format is invalid.");

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
    stopCore();

    int selection = 0;

    while (paused)
    {
        u32 pressed = menuScreen("NoiES", "", "", {}, pauseNames, {}, &selection);

        if (pressed & KEY_A)
        {
            if (selection == 1) // Save State
            {
                core::saveState();
            }
            else if (selection == 2) // Load State
            {
                core::loadState();
            }
            else if (selection == 3) // Settings
            {
                settingsMenu();
            }
            else if (selection == 4) // File Browser
            {
                core::closeRom();
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
    config::load();

    if (!fileBrowser())
    {
        deinitRenderer();
        return 0;
    }

    startCore();

    while (appletMainLoop())
    {
        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 released = hidKeysUp(CONTROLLER_P1_AUTO);

        for (int i = 0; i < 8; i++)
        {
            if (pressed & ((config::keyMap[i] == 0) ? defaultKeyMap[i] : BIT(config::keyMap[i] - 1)))
                core::pressKey(i);
            else if (released & ((config::keyMap[i] == 0) ? defaultKeyMap[i] : BIT(config::keyMap[i] - 1)))
                core::releaseKey(i);
        }

        if (pressed & defaultKeyMap[8])
        {
            if (!pauseMenu())
                break;
        }

        clearDisplay(0);
        mutex::lock(ppu::displayMutex);
        drawTexture(ppu::displayBuffer, 256, 240, 0, false, 256, 0, 768, 720);
        mutex::unlock(ppu::displayMutex);
        refreshDisplay();
    }

    core::closeRom();
    stopCore();
    deinitRenderer();
    return 0;
}
