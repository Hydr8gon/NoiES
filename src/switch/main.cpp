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
Thread coreThread, audioThread;

u32 *bufferPointer;
int bufferHeight, screenWidth, screenOffsetX;

u32 screenFiltering = 0;
u32 cropOverscan = 0;
u32 aspectRatio = 0;
string lastPath = "sdmc:/";

u32 keyMap[] =
{
    KEY_A, KEY_B, KEY_MINUS, KEY_PLUS,
    (KEY_DUP   | KEY_LSTICK_UP),   (KEY_DDOWN  | KEY_LSTICK_DOWN),
    (KEY_DLEFT | KEY_LSTICK_LEFT), (KEY_DRIGHT | KEY_LSTICK_RIGHT),
    (KEY_L | KEY_R)
};

const vector<config::Setting> platformSettings =
{
    { "screenFiltering", &screenFiltering, false },
    { "cropOverscan",    &cropOverscan,    false },
    { "aspectRatio",     &aspectRatio,     false },
    { "keyA",            &keyMap[0],       false },
    { "keyB",            &keyMap[1],       false },
    { "keySelect",       &keyMap[2],       false },
    { "keyStart",        &keyMap[3],       false },
    { "keyUp",           &keyMap[4],       false },
    { "keyDown",         &keyMap[5],       false },
    { "keyLeft",         &keyMap[6],       false },
    { "keyRight",        &keyMap[7],       false },
    { "keyMenu",         &keyMap[8],       false },
    { "lastPath",        &lastPath,        true  }
};

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
    "Pause Menu"
};

const vector<string> controlSubnames =
{
    "A Button", "B Button", "X Button", "Y Button",
    "Left Stick Click", "Right Stick Click",
    "L Button", "R Button", "ZL Button", "ZR Button",
    "Plus Button", "Minus Button",
    "D-Pad Left", "D-Pad Up", "D-Pad Right", "D-Pad Down",
    "Left Stick Left", "Left Stick Up", "Left Stick Right", "Left Stick Down",
    "Right Stick Left", "Right Stick Up", "Right Stick Right", "Right Stick Down"
};

const vector<string> settingNames =
{
    "Frame Limiter",
    "Disable Sprite Limit",
    "Screen Filtering",
    "Crop Overscan",
    "Aspect Ratio"
};

const vector<vector<string>> settingSubnames =
{
    { "Off", "On" },
    { "Off", "On" },
    { "Off", "On" },
    { "Off", "On" },
    { "Pixel Perfect", "4:3", "16:9" }
};

const vector<u32*> settingValues =
{
    &config::frameLimiter,
    &config::disableSpriteLimit,
    &screenFiltering,
    &cropOverscan,
    &aspectRatio
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
    AudioOutBuffer *audioBuffer;
    u32 count;

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

void setScreenLayout()
{
    if (cropOverscan)
    {
        bufferPointer = &ppu::displayBuffer[256 * 8];
        bufferHeight = 224;
    }
    else
    {
        bufferPointer = ppu::displayBuffer;
        bufferHeight = 240;
    }

    if (aspectRatio == 0) // Pixel Perfect
        screenWidth = (cropOverscan ? 823 : 768);
    else if (aspectRatio == 1) // 4:3
        screenWidth = 960;
    else // 16:9
        screenWidth = 1280;

    screenOffsetX = (1280 - screenWidth) / 2;
    setTextureFiltering(screenFiltering);
}

void startCore()
{
    paused = false;
    appletLockExit();
    audoutInitialize();
    audoutStartAudioOut();
    setupAudioBuffer();
    setScreenLayout();
    threadCreate(&coreThread, runCore, NULL, NULL, 0x8000, 0x30, 1);
    threadStart(&coreThread);
    threadCreate(&audioThread, audioOutput, NULL, NULL, 0x8000, 0x30, 0);
    threadStart(&audioThread);
}

void stopCore()
{
    paused = true;
    threadWaitForExit(&coreThread);
    threadClose(&coreThread);
    threadWaitForExit(&audioThread);
    threadClose(&audioThread);
    audoutStopAudioOut();
    audoutExit();
    appletUnlockExit();
}

void controlsMenu()
{
    int selection = 0;

    while (true)
    {
        vector<string> controlSubitems;
        for (unsigned int i = 0; i < controlNames.size(); i++)
        {
            if (keyMap[i] == 0)
            {
                controlSubitems.push_back("None");
            }
            else
            {
                string subitem;
                int count = 0;
                for (unsigned int j = 0; j < controlSubnames.size(); j++)
                {
                    if (keyMap[i] & BIT(j))
                    {
                        count++;
                        if (count < 5)
                        {
                            subitem += controlSubnames[j] + ", ";
                        }
                        else
                        {
                            subitem += "...";
                            break;
                        }
                    }
                }
                controlSubitems.push_back(subitem.substr(0, subitem.size() - ((count == 5) ? 0 : 2)));
            }
        }

        u32 pressed = menuScreen("Controls", "", "Clear", {}, controlNames, controlSubitems, &selection);

        if (pressed & KEY_A)
        {
            pressed = 0;
            while (pressed == 0 || pressed > KEY_RSTICK_DOWN)
                pressed = messageScreen("Controls", {"Press a button to add a mapping to: " + controlNames[selection]}, false);
            keyMap[selection] |= pressed;
        }
        else if (pressed & KEY_B)
        {
            return;
        }
        else if ((pressed & KEY_X) && !(pressed & KEY_TOUCH))
        {
            keyMap[selection] = 0;
        }
    }
}

void settingsMenu()
{
    int selection = 0;

    while (true)
    {
        vector<string> settingSubitems;
        for (unsigned int i = 0; i < settingNames.size(); i++)
            settingSubitems.push_back(settingSubnames[i][*settingValues[i]]);

        u32 pressed = menuScreen("Settings", "", "Controls", {}, settingNames, settingSubitems, &selection);

        if (pressed & KEY_A)
        {
            (*settingValues[selection])++;
            if (*settingValues[selection] >= settingSubnames[selection].size())
                *settingValues[selection] = 0;
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
    string romPath = lastPath;
    int selection = 0;

    while (true)
    {
        vector<string> files = dirContents(romPath, ".nes");
        vector<Icon> icons;

        for (unsigned int i = 0; i < files.size(); i++)
        {
            if (files[i].find(".nes", (files[i].length() - 4)) != string::npos)
                icons.push_back({fileIcon, 64});
            else
                icons.push_back({folderIcon, 64});
        }

        u32 pressed = menuScreen("NoiES", "Exit", "Settings", icons, files, {}, &selection);

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

                lastPath = romPath.substr(0, romPath.rfind("/"));
                config::save();
                return true;
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
    config::load(platformSettings);

    if (!fileBrowser())
    {
        deinitRenderer();
        return 0;
    }

    startCore();

    while (appletMainLoop())
    {
        hidScanInput();
        u64 pressed[] = { hidKeysDown(CONTROLLER_P1_AUTO), hidKeysDown(CONTROLLER_PLAYER_2) };
        u64 released[] = { hidKeysUp(CONTROLLER_P1_AUTO), hidKeysUp(CONTROLLER_PLAYER_2) };

        for (int i = 0; i < 2; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                if (pressed[i] & keyMap[j])
                    core::pressKey(i, j);
                else if (released[i] & keyMap[j])
                    core::releaseKey(i, j);
            }
        }

        if (pressed[0] & keyMap[8])
        {
            if (!pauseMenu())
                break;
        }

        clearDisplay(0);
        mutex::lock(ppu::displayMutex);
        drawImage(bufferPointer, 256, bufferHeight, false, screenOffsetX, 0, screenWidth, 720, 0);
        mutex::unlock(ppu::displayMutex);
        refreshDisplay();
    }

    core::closeRom();
    stopCore();
    deinitRenderer();
    return 0;
}
