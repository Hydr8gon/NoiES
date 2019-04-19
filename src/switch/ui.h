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

extern ColorSetId systemTheme;
extern u32 *fileIcon;
extern u32 *folderIcon;

u32 rgbaToU32(u8 r, u8 g, u8 b, u8 a);

void initRenderer();
void deinitRenderer();

void drawImage(u32 *image, int imageWidth, int imageHeight, bool reverse, float x, float y, float width, float height, int rotation);
void drawString(string str, float x, float y, int size, bool right, u32 color);
void drawLine(float x1, float y1, float x2, float y2, u32 color);
void setTextureFiltering(bool enabled);
void clearDisplay(u8 color);
void refreshDisplay();

u32 menuScreen(string title, string actionPlus, string actionX, vector<Icon> icons, vector<string> items, vector<string> subitems, int *selection);
u32 messageScreen(string title, vector<string> text, bool exit);

void setupAudioBuffer();

vector<string> dirContents(string directory, string extension);

#endif // UI_H
