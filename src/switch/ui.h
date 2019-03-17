#ifndef UI_H
#define UI_H

#include <string>
#include <vector>
#include <switch.h>

using namespace std;

typedef struct
{
    u32 *texture;
    int size;
} Icon;

typedef struct
{
    vector<string> names;
    int *value;
} Value;

extern ColorSetId systemTheme;

u32 *bmpTexture(string filename);

void initRenderer();
void deinitRenderer();

void drawTexture(u32 *texture, int texWidth, int texHeight, int rotation, bool reverse, float x, float y, float width, float height);
void drawString(string str, float x, float y, int size, bool color, bool right);
void drawLine(float x1, float y1, float x2, float y2, bool color);
void setTextureFiltering(bool enabled);
void clearDisplay(u8 color);
void refreshDisplay();

u32 menuScreen(string title, string actionPlus, string actionX, vector<Icon> icons, vector<string> items, vector<Value> values, int *selection);
u32 messageScreen(string title, vector<string> text, bool exit);

vector<string> dirContents(string directory, string extension);

#endif // UI_H
