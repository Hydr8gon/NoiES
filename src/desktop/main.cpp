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

#include <thread>
#include "GL/glut.h"
#include "portaudio.h"

#include "../core.h"
#include "../config.h"
#include "../ppu.h"
#include "../apu.h"
#include "../mutex.h"

bool requestSave, requestLoad;

const char defaultKeyMap[] = { 'l', 'k', 'g', 'h', 'w', 's', 'a', 'd' };

void runCore()
{
    while (true)
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

void draw()
{
    mutex::lock(ppu::displayMutex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 240, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ppu::displayBuffer);
    mutex::unlock(ppu::displayMutex);
    glBegin(GL_QUADS);
    glTexCoord2i(1, 1); glVertex2f( 1, -1);
    glTexCoord2i(0, 1); glVertex2f(-1, -1);
    glTexCoord2i(0, 0); glVertex2f(-1,  1);
    glTexCoord2i(1, 0); glVertex2f( 1,  1);
    glEnd();
    glFlush();
    glutPostRedisplay();
}

void keyDown(unsigned char key, int x, int y)
{
    for (int i = 0; i < 8; i++)
    {
        if (key == ((config::keyMap[i] == 0) ? defaultKeyMap[i] : config::keyMap[i]))
            core::pressKey(i);
    }
}

void keyUp(unsigned char key, int x, int y)
{
    for (int i = 0; i < 8; i++)
    {
        if (key == ((config::keyMap[i] == 0) ? defaultKeyMap[i] : config::keyMap[i]))
            core::releaseKey(i);
    }
}

int audioCallback(const void *in, void *out, unsigned long frames,
                  const PaStreamCallbackTimeInfo *info, PaStreamCallbackFlags flags, void *data)
{
    int16_t *curOut = (int16_t*)out;
    for (int i = 0; i < frames; i++)
        *curOut++ = apu::audioSample(2.5f);
    return 0;
}

void onMenuSelect(int selection)
{
    if (selection == 0) // Save State
        requestSave = true;
    else // Load State
        requestLoad = true;
}

void onExit()
{
    core::closeRom();
    config::save();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Please specify a ROM to load.\n");
        return 1;
    }

    if (core::loadRom(argv[1]) != 0)
        return 1;

    config::load();

    glutInit(&argc, argv);
    glutInitWindowSize(256, 240);
    glutCreateWindow("NoiES");
    glEnable(GL_TEXTURE_2D);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, config::screenFiltering ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, config::screenFiltering ? GL_LINEAR : GL_NEAREST);

    PaStream *stream;
    Pa_Initialize();
    Pa_OpenDefaultStream(&stream, 0, 1, paInt16, 44100, 256, audioCallback, NULL);
    Pa_StartStream(stream);

    glutCreateMenu(onMenuSelect);
    glutAddMenuEntry("Save State", 0);
    glutAddMenuEntry("Load State", 1);
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    atexit(onExit);
    glutDisplayFunc(draw);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);

    std::thread core(runCore);
    glutMainLoop();

    return 0;
}
