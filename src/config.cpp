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

#include <cstdio>
#include <string>
#include <vector>

int disableSpriteLimit;
int screenFiltering;

typedef struct
{
    std::string name;
    int *value;
} Setting;

const std::vector<Setting> settings =
{
    { "disableSpriteLimit", &disableSpriteLimit },
    { "screenFiltering",    &screenFiltering    }
};

void loadConfig()
{
    FILE *config = fopen("noies.ini", "r");
    if (!config)
        return;

    // Search for setting names and load the corresponding value when found
    char read[256];
    while (fgets(read, 256, config) != NULL)
    {
        std::string line = read;
        for (unsigned int i = 0; i < settings.size(); i++)
        {
            if (line.substr(0, line.rfind("=")) == settings[i].name)
                *settings[i].value = stoi(line.substr(line.rfind("=") + 1));
        }
    }

    fclose(config);
}

void saveConfig()
{
    FILE *config = fopen("noies.ini", "w");

    // Save all setting names and values to the file
    for (unsigned int i = 0; i < settings.size(); i++)
        fputs((settings[i].name + "=" + std::to_string(*settings[i].value) +"\n").c_str(), config);

    fclose(config);
}
