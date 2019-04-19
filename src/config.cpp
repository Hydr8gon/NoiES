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

#include "config.h"

namespace config
{

uint32_t disableSpriteLimit = 0;
uint32_t frameLimiter = 1;

vector<Setting> settings =
{
    { "disableSpriteLimit", &disableSpriteLimit, false },
    { "frameLimiter",       &frameLimiter,       false }
};

void load(vector<Setting> platformSettings)
{
    // Include any platform-specific settings
    settings.insert(settings.end(), platformSettings.begin(), platformSettings.end());

    FILE *config = fopen("noies.ini", "r");
    if (!config)
        return;

    // Search for setting names in the config file and load their values when found
    char read[256];
    while (fgets(read, 256, config) != NULL)
    {
        string line = read;
        for (unsigned int i = 0; i < settings.size(); i++)
        {
            int split = line.rfind("=");
            string name = line.substr(0, split);
            if (name == settings[i].name)
            {
                string value = line.substr(split + 1, line.size() - split - 2);
                if (settings[i].isString)
                    *((string*)settings[i].value) = value;
                else if (value[0] >= 0x30 && value[0] <= 0x39)
                    *((uint32_t*)settings[i].value) = stoi(value);
            }
        }
    }

    fclose(config);
}

void save()
{
    FILE *config = fopen("noies.ini", "w");

    // Save all setting names and values to the config file
    for (unsigned int i = 0; i < settings.size(); i++)
    {
        string value;
        if (settings[i].isString)
            value = *(string*)settings[i].value;
        else
            value = to_string(*(uint32_t*)settings[i].value).c_str();
        fputs((settings[i].name + '=' + value + '\n').c_str(), config);
    }

    fclose(config);
}

}
