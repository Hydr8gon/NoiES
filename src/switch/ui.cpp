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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <malloc.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

#include "ui.h"

ColorSetId systemTheme;
u32 *font, *fileIcon, *folderIcon;
u32 uiPalette[6];

EGLDisplay display;
EGLContext context;
EGLSurface surface;
GLuint program, vbo, texture;

bool showSelector = true;

AudioOutBuffer audioBuffers[2];
s16 *audioData[2];

const int charWidths[] =
{
    11,  9, 11, 20, 18, 28, 24,  7, 12, 12,
    14, 24,  9, 12,  9, 16, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21,  9,  9, 26, 24,
    26, 18, 28, 24, 21, 24, 26, 20, 20, 27,
    23,  9, 17, 21, 16, 31, 27, 29, 19, 29,
    20, 18, 21, 26, 24, 37, 21, 21, 24, 12,
    16, 12, 18, 16,  9, 20, 21, 18, 21, 20,
    10, 20, 20,  8, 12, 19,  9, 30, 20, 21,
    21, 21, 12, 16, 12, 20, 17, 29, 17, 17,
    16,  9,  8,  9, 12,  0, 40, 40, 40, 40
};

typedef struct
{
    float position[2];
    float texCoord[2];
} Vertex;

const char *vertexShader = R"(
    #version 330 core
    precision mediump float;

    layout (location = 0) in vec2 inPos;
    layout (location = 1) in vec2 inTexCoord;
    out vec2 vtxTexCoord;

    void main()
    {
        gl_Position = vec4(-1.0 + inPos.x / 640, 1.0 - inPos.y / 360, 0.0, 1.0);
        vtxTexCoord = inTexCoord;
    }
)";

const char *fragmentShader = R"(
    #version 330 core
    precision mediump float;

    in vec2 vtxTexCoord;
    out vec4 fragColor;
    uniform sampler2D texDiffuse;

    void main()
    {
        fragColor = texture(texDiffuse, vtxTexCoord);
    };
)";

u32 rgbaToU32(u8 r, u8 g, u8 b, u8 a)
{
    return (r << 24) | (g << 16) | (b << 8) | a;
}

u32 *bmpToTexture(string filename)
{
    FILE *bmp = fopen(filename.c_str(), "rb");
    if (!bmp)
        return NULL;

    // Read the header
    u8 header[70];
    fread(header, sizeof(u8), 70, bmp);
    int width = *(int*)&header[18];
    int height = *(int*)&header[22];

    // Convert the bitmap data to RGBA format
    u32 *tex = new u32[width * height];
    for (int y = 1; y <= height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            u8 b; fread(&b, sizeof(u8), 1, bmp);
            u8 g; fread(&g, sizeof(u8), 1, bmp);
            u8 r; fread(&r, sizeof(u8), 1, bmp);
            u8 a; fread(&a, sizeof(u8), 1, bmp);
            tex[(height - y) * width + x] = rgbaToU32(r, g, b, a);
        }
    }

    fclose(bmp);
    return tex;
}

int stringWidth(string str)
{
    int width = 0;
    for (unsigned int i = 0; i < str.size(); i++)
        width += charWidths[str[i] - 32];
    return width;
}

void loadTheme()
{
    // Get the current system theme
    setsysInitialize();
    setsysGetColorSetId(&systemTheme);
    setsysExit();

    // Load the font bitmaps corresponding to the theme
    romfsInit();
    string theme = (systemTheme == ColorSetId_Light) ? "light" : "dark";
    font = bmpToTexture("romfs:/font.bmp");
    fileIcon = bmpToTexture("romfs:/file-" + theme + ".bmp");
    folderIcon = bmpToTexture("romfs:/folder-" + theme + ".bmp");
    romfsExit();

    // Set the theme colors
    if (systemTheme == ColorSetId_Light)
    {
        uiPalette[0] = rgbaToU32(235, 235, 235, 255);
        uiPalette[1] = rgbaToU32( 45,  45,  45, 255);
        uiPalette[2] = rgbaToU32(205, 205, 205, 255);
        uiPalette[3] = rgbaToU32(255, 255, 255, 255);
        uiPalette[4] = rgbaToU32( 50, 215, 210, 255);
        uiPalette[5] = rgbaToU32( 50,  80, 240, 255);
    }
    else
    {
        uiPalette[0] = rgbaToU32( 45,  45,  45, 255);
        uiPalette[1] = rgbaToU32(255, 255, 255, 255);
        uiPalette[2] = rgbaToU32( 75,  75,  75, 255);
        uiPalette[3] = rgbaToU32( 35,  35,  35, 255);
        uiPalette[4] = rgbaToU32( 85, 185, 225, 255);
        uiPalette[5] = rgbaToU32(  0, 255, 200, 255);
    }
}

void initRenderer()
{
    // Initialize EGL
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_API);
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, {}, &config, 1, &numConfigs);
    surface = eglCreateWindowSurface(display, config, nwindowGetDefault(), NULL);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, {});
    eglMakeCurrent(display, surface, surface, context);

    gladLoadGL();

    GLint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vertexShader, NULL);
    glCompileShader(vertShader);

    GLint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fragmentShader, NULL);
    glCompileShader(fragShader);

    program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    glEnableVertexAttribArray(1);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    loadTheme();
}

void deinitRenderer()
{
    glDeleteProgram(program);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);

    // Deinitialize EGL
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
}

void setSurface()
{
    glUseProgram(program);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 1280, 720);
    glBindVertexArray(0);
}

void drawImage(u32 *image, int imageWidth, int imageHeight, bool reverse, float x, float y, float width, float height, int rotation)
{
    Vertex dimensions[] =
    {
        { { x + width, y + height }, { 1.0f, 1.0f } },
        { { x,         y + height }, { 0.0f, 1.0f } },
        { { x,         y          }, { 0.0f, 0.0f } },
        { { x + width, y          }, { 1.0f, 0.0f } }
    };

    // Rotate the image 90 degrees clockwise for every rotation
    for (int i = 0; i < rotation; i++)
    {
        int size = sizeof(dimensions[0].position);
        Vertex *copy = new Vertex[sizeof(dimensions) / sizeof(Vertex)];
        memcpy(copy, dimensions, sizeof(dimensions));
        for (int k = 0; k < 8; k += 4)
        {
            memcpy(dimensions[k    ].position, copy[k + 1].position, size);
            memcpy(dimensions[k + 1].position, copy[k + 2].position, size);
            memcpy(dimensions[k + 2].position, copy[k + 3].position, size);
            memcpy(dimensions[k + 3].position, copy[k    ].position, size);
        }
        delete[] copy;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(dimensions), dimensions, GL_DYNAMIC_DRAW);

    GLenum format = reverse ? GL_BGRA : GL_RGBA;
    GLenum type = reverse ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_INT_8_8_8_8;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 0, format, type, image);

    setSurface();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void drawString(string str, float x, float y, int size, bool right, u32 color)
{
    int width = stringWidth(str);
    u32 *tex = new u32[width * 48];
    int currentX = 0;

    // Copy the characters from the font bitmap into the string texture
    for (unsigned int i = 0; i < str.size(); i++)
    {
        int col = (str[i] - 32) % 10;
        int row = (str[i] - 32) / 10;

        for (int j = 0; j < 48; j++)
            for (int k = 0; k < charWidths[str[i] - 32]; k++)
                tex[j * width + currentX + k] = (color & ~0xFF) | (font[(row * 512 + col) * 48 + j * 512 +  k] & 0xFF);

        currentX += charWidths[str[i] - 32];
    }

    if (right) // Align the string to the right
        x -= width * size / 48;

    drawImage(tex, width, 48, false, x, y, width * size / 48, size, 0);
    delete[] tex;
}

void drawLine(float x1, float y1, float x2, float y2, u32 color)
{
    Vertex dimensions[] =
    {
        { { x1, y1 }, { 0.0f, 0.0f } },
        { { x2, y2 }, { 0.0f, 0.0f } }
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(dimensions), dimensions, GL_DYNAMIC_DRAW);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, &color);

    setSurface();
    glDrawArrays(GL_LINES, 0, 2);
}

void setTextureFiltering(bool enabled)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, enabled ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, enabled ? GL_LINEAR : GL_NEAREST);
}

void clearDisplay(u8 color)
{
    setSurface();
    float clear = (float)color / 255;
    glClearColor(clear, clear, clear, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void refreshDisplay()
{
    eglSwapBuffers(display, surface);
}

u32 menuScreen(string title, string actionPlus, string actionX, vector<Icon> icons, vector<string> items, vector<string> subitems, int *selection)
{
    if (actionPlus != "")
        actionPlus = "\x83 " + actionPlus + "     ";
    if (actionX != "")
        actionX = "\x82 " + actionX + "     ";
    string actionB = "\x81 Back     ";
    string actionA = "\x80 OK";

    unsigned int boundsAB    = 1218 - (stringWidth(actionA) + charWidths[0] * 2.5) * 34 / 48;
    unsigned int boundsBX    = boundsAB    - stringWidth(actionB)    * 34 / 48;
    unsigned int boundsXPlus = boundsBX    - stringWidth(actionX)    * 34 / 48;
    unsigned int boundsPlus  = boundsXPlus - stringWidth(actionPlus) * 34 / 48;

    unsigned int position = *selection;
    bool upHeld = false;
    bool downHeld = false;
    bool scroll = false;
    chrono::steady_clock::time_point timeHeld;

    bool touchStarted = false;
    bool touchScroll = false;
    touchPosition touch, touchMove;

    setTextureFiltering(true);

    while (true)
    {
        clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);

        drawString(title, 72, 30, 42, false, uiPalette[1]);
        drawLine(30, 88, 1250, 88, uiPalette[1]);
        drawLine(30, 648, 1250, 648, uiPalette[1]);
        drawString(actionPlus + actionX + actionB + actionA, 1218, 667, 34, true, uiPalette[1]);

        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 released = hidKeysUp(CONTROLLER_P1_AUTO);

        if (pressed & KEY_UP && position > 0)
        {
            if (!showSelector)
                showSelector = true;
            else
                position--;
            upHeld = true;
            timeHeld = chrono::steady_clock::now();
        }
        else if (pressed & KEY_DOWN && position < items.size() - 1)
        {
            if (!showSelector)
                showSelector = true;
            else
                position++;
            downHeld = true;
            timeHeld = chrono::steady_clock::now();
        }
        else if ((pressed & (KEY_A | KEY_B)) || (actionX != "" && (pressed & KEY_X)) || (actionPlus != "" && (pressed & KEY_PLUS)))
        {
            if (!(pressed & KEY_A) || showSelector)
            {
                showSelector = true;
                *selection = position;
                return pressed;
            }
            showSelector = true;
        }

        if (released & KEY_UP)
        {
            upHeld = false;
            scroll = false;
        }
        if (released & KEY_DOWN)
        {
            downHeld = false;
            scroll = false;
        }

        // Scroll continuously when up or down is held
        if ((upHeld && position > 0) || (downHeld && position < items.size() - 1))
        {
            chrono::duration<double> elapsed = chrono::steady_clock::now() - timeHeld;
            if (!scroll && elapsed.count() > 0.5f)
                scroll = true;
            if (scroll && elapsed.count() > 0.1f)
            {
                position += (upHeld && position > 0) ? -1 : 1;
                timeHeld = chrono::steady_clock::now();
            }
        }

        if (hidTouchCount() > 0)
        {
            if (!touchStarted)
            {
                hidTouchRead(&touch, 0);
                touchStarted = true;
                touchScroll = false;
                showSelector = false;
            }
            hidTouchRead(&touchMove, 0);

            if (touchScroll)
            {
                // Scroll with a dragged touch
                int newPos = *selection + (int)(touch.py - touchMove.py) / 70;
                if (items.size() <= 7)
                    position = 0;
                else if (newPos > (int)items.size() - 4)
                    position = items.size() - 4;
                else if (newPos < 3)
                    position = 3;
                else
                    position = newPos;
             }
             else if (touchMove.px > touch.px + 25 || touchMove.px < touch.px - 25 || touchMove.py > touch.py + 25 || touchMove.py < touch.py - 25)
             {
                 // Prepare to scroll with a dragged touch
                 touchScroll = true;
                 if (items.size() <= 7)
                     *selection = position;
                 if (position > items.size() - 4)
                     *selection = items.size() - 4;
                 else if (position < 3)
                     *selection = 3;
                 else
                     *selection = position;
            }
        }
        else
        {
            // Simulate a button press if the button text is touched
            if (!touchScroll && touch.py >= 650)
            {
                if (touch.px >= boundsBX && touch.px < boundsAB)
                    return KEY_B | KEY_TOUCH;
                else if (touch.px >= boundsXPlus && touch.px < boundsBX)
                    return KEY_X | KEY_TOUCH;
                else if (touch.px >= boundsPlus && touch.px < boundsXPlus)
                    return KEY_PLUS | KEY_TOUCH;
            }
            touchStarted = false;
        }

        if (items.size() > 0)
            drawLine(90, 124, 1190, 124, uiPalette[2]);

        for (unsigned int i = 0; i < 7; i++)
        {
            if (i < items.size())
            {
                unsigned int row;
                if (position < 4 || items.size() <= 7)
                    row = i;
                else if (position > items.size() - 4)
                    row = items.size() - 7 + i;
                else
                    row = i + position - 3;

                // Simulate an A press on a selection if its row is touched
                if (!touchStarted && !touchScroll && touch.px >= 90 && touch.px < 1190 && touch.py >= 124 + i * 70 && touch.py < 194 + i * 70)
                {
                    *selection = row;
                    return KEY_A | KEY_TOUCH;
                }

                // Draw the selection box and row lines
                if (row == position && showSelector)
                {
                    drawImage(&uiPalette[3], 1, 1, false,   90, 125 + i * 70, 1100, 69, 0);
                    drawImage(&uiPalette[4], 1, 1, false,   89, 121 + i * 70, 1103,  5, 0);
                    drawImage(&uiPalette[4], 1, 1, false,   89, 191 + i * 70, 1103,  5, 0);
                    drawImage(&uiPalette[4], 1, 1, false,   88, 122 + i * 70,    5, 73, 0);
                    drawImage(&uiPalette[4], 1, 1, false, 1188, 122 + i * 70,    5, 73, 0);
                }
                else
                {
                    drawLine(90, 194 + i * 70, 1190, 194 + i * 70, uiPalette[2]);
                }

                // Draw the rows
                if (icons.size() > row)
                {
                    drawImage(icons[row].texture, icons[row].size, icons[row].size, false, 105, 126 + i * 70, 64, 64, 0);
                    drawString(items[row], 184, 140 + i * 70, 38, false, uiPalette[1]);
                }
                else
                {
                    drawString(items[row], 105, 140 + i * 70, 38, false, uiPalette[1]);
                }
                if (subitems.size() > row)
                    drawString(subitems[row], 1175, 143 + i * 70, 32, true, uiPalette[5]);
            }
        }

        refreshDisplay();
    }
}

u32 messageScreen(string title, vector<string> text, bool exit)
{
    clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);

    drawString(title, 72, 30, 42, false, uiPalette[1]);
    drawLine(30, 88, 1250, 88, uiPalette[1]);
    drawLine(30, 648, 1250, 648, uiPalette[1]);

    if (exit)
        drawString("\x83 Exit", 1218, 667, 34, true, uiPalette[1]);

    for (unsigned int i = 0; i < text.size(); i++)
        drawString(text[i], 90, 124 + i * 38, 38, false, uiPalette[1]);

    refreshDisplay();

    while (true)
    {
        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if ((!exit && pressed) || (pressed & KEY_PLUS))
            return pressed;
    }
}

void setupAudioBuffer()
{
    for (int i = 0; i < 2; i++)
    {
        int size = 1024 * 2 * sizeof(s16);
        int alignedSize = (size + 0xFFF) & ~0xFFF;
        audioData[i] = (s16*)memalign(0x1000, size);
        memset(audioData[i], 0, alignedSize);
        audioBuffers[i].next = NULL;
        audioBuffers[i].buffer = audioData[i];
        audioBuffers[i].buffer_size = alignedSize;
        audioBuffers[i].data_size = size;
        audioBuffers[i].data_offset = 0;
        audoutAppendAudioOutBuffer(&audioBuffers[i]);
    }
}

vector<string> dirContents(string directory, string extension)
{
    vector<string> contents;
    DIR *dir = opendir(directory.c_str());
    dirent *entry;

    while ((entry = readdir(dir)))
    {
        string name = entry->d_name;
        if (entry->d_type == DT_DIR || name.find(extension, (name.length() - extension.length())) != string::npos)
            contents.push_back(name);
    }

    closedir(dir);
    sort(contents.begin(), contents.end());
    return contents;
}
