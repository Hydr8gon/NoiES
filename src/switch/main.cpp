#include "ui.h"
#include "../core.h"

string romPath = "sdmc:/";

int outSamples;
AudioOutBuffer audioBuffer, *audioReleasedBuffer;

const u32 keymap[] = { KEY_A, KEY_B, KEY_MINUS, KEY_PLUS, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };

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
    {
        pcvSetClockRate(PcvModule_Cpu, 1785000000);
        setupAudioBuffer();
    }
}

void runCore(void *args)
{
    while (true)
        runCycle();
}

void audioOutput(void *args)
{
    while (true)
    {
        for (int i = 0; i < outSamples; i++)
            ((s16*)audioBuffer.buffer)[i] = audioSample(1);
        audoutPlayBuffer(&audioBuffer, &audioReleasedBuffer);
    }
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

int main(int argc, char **argv)
{
    initRenderer();

    if (!fileBrowser())
    {
        deinitRenderer();
        return 0;
    }

    if (!loadRom(const_cast<char*>(romPath.c_str())))
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

    AppletHookCookie cookie;

    appletLockExit();
    appletHook(&cookie, onAppletHook, NULL);
    audoutInitialize();
    audoutStartAudioOut();
    setupAudioBuffer();
    pcvInitialize();
    pcvSetClockRate(PcvModule_Cpu, 1785000000);

    Thread core, audio;
    threadCreate(&core, runCore, NULL, 0x80000, 0x30, 1);
    threadStart(&core);
    threadCreate(&audio, audioOutput, NULL, 0x80000, 0x30, 2);
    threadStart(&audio);

    setTextureFiltering(false);

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

        clearDisplay(0);
        drawTexture(displayBuffer, 256, 240, 0, false, 256, 0, 768, 720);
        refreshDisplay();
    }

    pcvSetClockRate(PcvModule_Cpu, 1020000000);
    pcvExit();
    audoutStopAudioOut();
    audoutExit();
    appletUnhook(&cookie);
    appletUnlockExit();
    deinitRenderer();
    return 0;
}