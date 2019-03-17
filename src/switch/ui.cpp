#include "ui.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

ColorSetId systemTheme;
u32 *font, *fontColor;

EGLDisplay display;
EGLContext context;
EGLSurface surface;
GLuint program, vao, vbo, texture;

const int charWidth[] =
{
    11, 10, 11, 20, 19, 28, 25,  7, 12, 12,
    15, 25,  9, 11,  9, 17, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21,  9,  9, 26, 25,
    26, 18, 29, 24, 21, 24, 27, 20, 20, 27,
    24, 10, 17, 21, 16, 31, 27, 29, 20, 29,
    20, 19, 21, 26, 25, 37, 22, 21, 24, 12,
    17, 12, 18, 17, 10, 20, 22, 19, 22, 20,
    10, 22, 20,  9, 12, 19,  9, 30, 20, 22,
    22, 22, 13, 17, 13, 20, 17, 29, 18, 18,
    17, 10,  9, 10, 25, 32, 40, 40, 40, 40
};

typedef struct
{
    float position[2];
    float texCoord[2];
} Vertex;

const char *vertexShader =
    "#version 330 core\n"
    "precision mediump float;"

    "layout (location = 0) in vec2 inPos;"
    "layout (location = 1) in vec2 inTexCoord;"
    "out vec2 vtxTexCoord;"

    "void main()"
    "{"
        "gl_Position = vec4(-1.0 + inPos.x / 640, 1.0 - inPos.y / 360, 0.0, 1.0);"
        "vtxTexCoord = inTexCoord;"
    "}";

const char *fragmentShader =
    "#version 330 core\n"
    "precision mediump float;"

    "in vec2 vtxTexCoord;"
    "out vec4 fragColor;"
    "uniform sampler2D texDiffuse;"

    "void main()"
    "{"
        "fragColor = texture(texDiffuse, vtxTexCoord);"
    "}";

u32 *bmpTexture(string filename)
{
    FILE *bmp = fopen(filename.c_str(), "rb");

    // Read the header
    u8 header[54];
    fread(header, sizeof(u8), 54, bmp);
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
            tex[(height - y) * width + x] = (r << 24) | (g << 16) | (b << 8);
        }
    }

    fclose(bmp);
    return tex;
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
    font = bmpTexture("romfs:/font-" + theme + ".bmp");
    fontColor = bmpTexture("romfs:/fontcolor-" + theme + ".bmp");
    romfsExit();
}

void initRenderer()
{
    EGLConfig config;
    EGLint numConfigs;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_API);
    eglChooseConfig(display, {}, &config, 1, &numConfigs);
    surface = eglCreateWindowSurface(display, config, (char*)"", NULL);
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

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

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

    glUseProgram(program);

    loadTheme();
}

void deinitRenderer()
{
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    context = NULL;
    eglDestroySurface(display, surface);
    surface = NULL;
    eglTerminate(display);
    display = NULL;
}

void drawTexture(u32 *texture, int texWidth, int texHeight, int rotation, bool reverse, float x, float y, float width, float height)
{
    Vertex image[] =
    {
        { { x + width, y + height }, { 1.0f, 1.0f } },
        { { x,         y + height }, { 0.0f, 1.0f } },
        { { x,         y          }, { 0.0f, 0.0f } },
        { { x + width, y          }, { 1.0f, 0.0f } }
    };

    for (int i = 0; i < rotation; i++)
    {
        // Rotate the texture 90 degrees clockwise
        int size = sizeof(image[0].position);
        Vertex *copy = new Vertex[sizeof(image) / sizeof(Vertex)];
        memcpy(copy, image, sizeof(image));
        for (int k = 0; k < 8; k += 4)
        {
            memcpy(image[k    ].position, copy[k + 1].position, size);
            memcpy(image[k + 1].position, copy[k + 2].position, size);
            memcpy(image[k + 2].position, copy[k + 3].position, size);
            memcpy(image[k + 3].position, copy[k    ].position, size);
        }
        delete[] copy;
    }

    int format1 = reverse ? GL_BGRA : GL_RGBA;
    int format2 = reverse ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_INT_8_8_8_8;

    glBufferData(GL_ARRAY_BUFFER, sizeof(image), image, GL_DYNAMIC_DRAW);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight, 0, format1, format2, texture);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void drawString(string str, float x, float y, int size, bool color, bool right)
{
    // Find the total width of the string texture
    int width = 0;
    for (unsigned int i = 0; i < str.size(); i++)
        width += charWidth[str[i] - 32];

    u32 *fontSel = color ? fontColor : font;
    u32 *tex = new u32[width * 48];
    int currentX = 0;

    // Copy the characters from the font bitmap into the string texture
    for (unsigned int i = 0; i < str.size(); i++)
    {
        int col = (str[i] - 32) % 10;
        int row = (str[i] - 32) / 10;

        for (int j = 0; j < 48; j++)
            memcpy(&tex[j * width + currentX], &fontSel[(row * 512 + col) * 48 + j * 512], charWidth[str[i] - 32] * sizeof(u32));

        currentX += charWidth[str[i] - 32];
    }

    if (right) // Align the string to the right
        x -= width * size / 48;

    drawTexture(tex, width, 48, 0, false, x, y, width * size / 48, size);
    delete[] tex;
}

void drawLine(float x1, float y1, float x2, float y2, bool color)
{
    Vertex line[] =
    {
        { { x1, y1 }, { 0.0f, 0.0f } },
        { { x2, y2 }, { 0.0f, 0.0f } }
    };

    u8 tex[3];
    memset(tex, (systemTheme == ColorSetId_Light) ? (color ? 205 : 45) : (color ? 77 : 255), sizeof(tex));

    glBufferData(GL_ARRAY_BUFFER, sizeof(line), line, GL_DYNAMIC_DRAW);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glDrawArrays(GL_LINES, 0, 2);
}

void setTextureFiltering(bool enabled)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, enabled ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, enabled ? GL_LINEAR : GL_NEAREST);
}

void clearDisplay(u8 color)
{
    float clear = (float)color / 255;
    glClearColor(clear, clear, clear, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void refreshDisplay()
{
    eglSwapBuffers(display, surface);
}

u32 menuScreen(string title, string actionPlus, string actionX, vector<Icon> icons, vector<string> items, vector<Value> values, int *selection)
{
    string buttons;
    if (actionPlus != "")
        buttons += "\x83 " + actionPlus + "     ";
    if (actionX != "")
        buttons += "\x82 " + actionX + "     ";
    buttons += "\x81 Back     \x80 OK";

    unsigned int position = *selection;
    bool upHeld = false;
    bool downHeld = false;
    bool scroll = false;
    chrono::steady_clock::time_point timeHeld;

    setTextureFiltering(true);

    while (true)
    {
        clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);

        drawString(title, 72, 30, 42, false, false);
        drawLine(30, 88, 1250, 88, false);
        drawLine(30, 648, 1250, 648, false);
        drawString(buttons, 1218, 667, 34, false, true);

        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 released = hidKeysUp(CONTROLLER_P1_AUTO);

        if (pressed & KEY_UP && position > 0)
        {
            position--;
            upHeld = true;
            timeHeld = chrono::steady_clock::now();
        }
        else if (pressed & KEY_DOWN && position < items.size() - 1)
        {
            position++;
            downHeld = true;
            timeHeld = chrono::steady_clock::now();
        }
        else if (pressed & (KEY_A | KEY_B) || (actionX != "" && pressed & KEY_X) || (actionPlus != "" && pressed & KEY_PLUS))
        {
            *selection = position;
            return pressed;
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

        if (items.size() > 0)
            drawLine(90, 124, 1190, 124, true);

        // Draw the rows
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

                if (icons.size() > row)
                {
                    drawTexture(icons[row].texture, icons[row].size, icons[row].size, 0, false, 105, 127 + i * 70, 64, 64);
                    drawString(items[row], 184, 140 + i * 70, 38, row == position, false);
                }
                else
                {
                    drawString(items[row], 105, 140 + i * 70, 38, row == position, false);
                }

                if (values.size() > row && *values[row].value < (int)values[row].names.size())
                    drawString(values[row].names[*values[row].value], 1175, 143 + i * 70, 32, row == position, true);

                drawLine(90, 194 + i * 70, 1190, 194 + i * 70, true);
            }
        }

        refreshDisplay();
    }
}

u32 messageScreen(string title, vector<string> text, bool exit)
{
    clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);

    drawString(title, 72, 30, 42, false, false);
    drawLine(30, 88, 1250, 88, false);
    drawLine(30, 648, 1250, 648, false);

    if (exit)
        drawString("\x83 Exit", 1218, 667, 34, false, true);

    for (unsigned int i = 0; i < text.size(); i++)
        drawString(text[i], 90, 124 + i * 38, 38, false, false);

    refreshDisplay();

    while (true)
    {
        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        if ((!exit && pressed) || (pressed & KEY_PLUS))
            return pressed;
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